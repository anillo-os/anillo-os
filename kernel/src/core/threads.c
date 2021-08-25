#include <ferro/core/threads.private.h>
#include <ferro/core/mempool.h>
#include <ferro/core/panic.h>
#include <ferro/core/paging.h>

#include <libk/libk.h>

#include <stdatomic.h>

ferr_t fthread_retain(fthread_t* thread) {
	if (__atomic_fetch_add(&thread->reference_count, 1, __ATOMIC_RELAXED) == 0) {
		return ferr_permanent_outage;
	}

	return ferr_ok;
};

void fthread_release(fthread_t* thread) {
	if (__atomic_sub_fetch(&thread->reference_count, 1, __ATOMIC_ACQ_REL) != 0) {
		return;
	}

	if (fmempool_free(thread) != ferr_ok) {
		fpanic("Failed to free thread information structure");
	}
};

FERRO_NO_RETURN void fthread_exit(void* exit_data, size_t exit_data_size, bool copy_exit_data) {
	fthread_private_t* thread = (void*)fthread_current();
	void* data = exit_data;
	size_t data_size = exit_data_size;

	if (copy_exit_data) {
		if (fmempool_allocate(exit_data_size, NULL, (void*)&data) != ferr_ok) {
			data = NULL;
			data_size = 0;
		}
		memcpy(data, exit_data, exit_data_size);
	}

	if (data) {
		flock_spin_intsafe_lock(&thread->thread.lock);
		thread->thread.exit_data = data;
		thread->thread.exit_data_size = data_size;
		if (copy_exit_data) {
			thread->thread.flags |= fthread_flag_exit_data_copied;
		}
		flock_spin_intsafe_unlock(&thread->thread.lock);
	}

	// when you kill your own thread, it should exit immediately
	fthread_kill_self();
	__builtin_unreachable();
};

ferr_t fthread_suspend(fthread_t* thread) {
	fthread_private_t* private_thread;
	fthread_state_execution_t prev_exec_state;
	ferr_t status = ferr_ok;

	if (!thread) {
		thread = fthread_current();
	}

	private_thread = (void*)thread;

	if (!private_thread->manager) {
		return ferr_invalid_argument;
	}

	flock_spin_intsafe_lock(&thread->lock);

	prev_exec_state = fthread_state_execution_read_locked(thread);

	if (prev_exec_state == fthread_state_execution_dead || (thread->state & fthread_state_pending_death) != 0) {
		status = ferr_permanent_outage;
	} else if (prev_exec_state == fthread_state_execution_suspended || (thread->state & fthread_state_pending_suspend) != 0) {
		status = ferr_already_in_progress;
	} else {
		thread->state |= fthread_state_pending_suspend;
		private_thread->manager->suspend(thread);
	}

	flock_spin_intsafe_unlock(&thread->lock);

	return status;
};

void fthread_suspend_self(void) {
	if (fthread_suspend(NULL) != ferr_ok) {
		fpanic("Failed to suspend own thread");
	}
};

ferr_t fthread_resume(fthread_t* thread) {
	// we don't accept `NULL` here because if you're suspended, you can't resume yourself. that's just not possible.
	fthread_private_t* private_thread = (void*)thread;
	fthread_state_execution_t prev_exec_state;
	ferr_t status = ferr_ok;

	if (!thread) {
		return ferr_invalid_argument;
	}

	if (!private_thread->manager) {
		return ferr_invalid_argument;
	}

	flock_spin_intsafe_lock(&thread->lock);

	prev_exec_state = fthread_state_execution_read_locked(thread);

	if (prev_exec_state == fthread_state_execution_dead || (thread->state & fthread_state_pending_death) != 0) {
		status = ferr_permanent_outage;
	} else if (prev_exec_state != fthread_state_execution_suspended && (thread->state & fthread_state_pending_suspend) == 0) {
		status = ferr_already_in_progress;
	} else {
		thread->state &= ~fthread_state_pending_suspend;
		private_thread->manager->resume(thread);
	}

	flock_spin_intsafe_unlock(&thread->lock);

	return status;
};

ferr_t fthread_kill(fthread_t* thread) {
	fthread_private_t* private_thread;
	fthread_state_execution_t prev_exec_state;
	ferr_t status = ferr_ok;

	if (!thread) {
		thread = fthread_current();
	}

	private_thread = (void*)thread;

	if (fthread_retain(thread) != ferr_ok) {
		return ferr_invalid_argument;
	}

	if (!private_thread->manager) {
		return ferr_invalid_argument;
	}

	flock_spin_intsafe_lock(&thread->lock);

	prev_exec_state = fthread_state_execution_read_locked(thread);

	if (prev_exec_state == fthread_state_execution_dead || (thread->state & fthread_state_pending_death) != 0) {
		status = ferr_already_in_progress;
	} else {
		thread->state |= fthread_state_pending_death;
		private_thread->manager->kill(thread);
	}

	flock_spin_intsafe_unlock(&thread->lock);

	fthread_release(thread);

	return status;
};

FERRO_NO_RETURN void fthread_kill_self(void) {
	if (fthread_kill(NULL) != ferr_ok) {
		fpanic("Failed to kill own thread");
	}
	__builtin_unreachable();
};

void fthread_interrupt_start(fthread_t* thread) {
	fthread_private_t* private_thread = (void*)thread;
	private_thread->manager->interrupted(thread);
};

void fthread_interrupt_end(fthread_t* thread) {
	fthread_private_t* private_thread = (void*)thread;
	private_thread->manager->ending_interrupt(thread);
};

void fthread_died(fthread_t* thread) {
	if ((thread->flags & fthread_flag_deallocate_stack_on_exit) != 0) {
		if (fpage_free_kernel(thread->stack_base, fpage_round_up_to_page_count(thread->stack_size)) != ferr_ok) {
			fpanic("Failed to free thread stack");
		}
	}
};

fthread_state_execution_t fthread_execution_state(fthread_t* thread) {
	fthread_state_execution_t result;
	flock_spin_intsafe_lock(&thread->lock);
	result = fthread_state_execution_read_locked(thread);
	flock_spin_intsafe_unlock(&thread->lock);
	return result;
};

ferr_t fthread_new(fthread_initializer_f initializer, void* data, void* stack_base, size_t stack_size, fthread_flags_t flags, fthread_t** out_thread) {
	fthread_private_t* new_thread = NULL;
	bool release_stack_on_fail = false;

	if (!initializer || !out_thread) {
		return ferr_invalid_argument;
	}

	if (!stack_base) {
		if (fpage_allocate_kernel(fpage_round_up_to_page_count(stack_size), &stack_base) != ferr_ok) {
			return ferr_temporary_outage;
		}

		release_stack_on_fail = true;
		flags |= fthread_flag_deallocate_stack_on_exit;
	}

	if (fmempool_allocate(sizeof(fthread_private_t), NULL, (void**)&new_thread) != ferr_ok) {
		if (release_stack_on_fail) {
			if (fpage_free_kernel(stack_base, fpage_round_up_to_page_count(stack_size)) != ferr_ok) {
				fpanic("Failed to free thread stack");
			}
		}
		return ferr_temporary_outage;
	}

	*out_thread = (void*)new_thread;

	// clear the thread
	memset(new_thread, 0, sizeof(*new_thread));

	flock_spin_intsafe_init(&new_thread->thread.lock);

	new_thread->thread.reference_count = 1;

	new_thread->thread.stack_base = stack_base;
	new_thread->thread.stack_size = stack_size;

	new_thread->thread.flags = flags;

	// the thread must start as suspended
	fthread_state_execution_write_locked(&new_thread->thread, fthread_state_execution_suspended);

	farch_thread_init_info(&new_thread->thread, initializer, data);

	return ferr_ok;
};
