/*
 * This file is part of Anillo OS
 * Copyright (C) 2021 Anillo OS Developers
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/**
 * @file
 *
 * Thread creation and management.
 */

#include <ferro/core/threads.private.h>
#include <ferro/core/mempool.h>
#include <ferro/core/panic.h>
#include <ferro/core/paging.h>
#include <ferro/core/waitq.private.h>
#include <ferro/core/workers.h>
#include <ferro/core/console.h>
#include <ferro/core/interrupts.h>
#include <ferro/gdbstub/gdbstub.h>

#include <libsimple/libsimple.h>

#include <stdatomic.h>

static void fthread_destroy_worker(void* context) {
	fthread_t* thread = context;

	fwaitq_wake_many(&thread->destroy_wait, SIZE_MAX);

	if (fmempool_free(thread) != ferr_ok) {
		fpanic("Failed to free thread information structure");
	}
};

static void fthread_destroy(fthread_t* thread) {
	fpanic_status(fwork_schedule_new(fthread_destroy_worker, thread, 0, NULL));
};

ferr_t fthread_retain(fthread_t* thread) {
	return frefcount_increment(&thread->reference_count);
};

void fthread_release(fthread_t* thread) {
	if (frefcount_decrement(&thread->reference_count) != ferr_permanent_outage) {
		return;
	}

	fthread_destroy(thread);
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
		simple_memcpy(data, exit_data, exit_data_size);
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

static void fthread_suspend_timeout_suspend_waiter(void* context) {
	volatile bool* do_wait = context;
	*do_wait = false;
};

ferr_t fthread_suspend_timeout(fthread_t* thread, bool wait, uint64_t timeout_value, fthread_timeout_type_t timeout_type) {
	fthread_private_t* private_thread;
	fthread_state_execution_t prev_exec_state;
	ferr_t status = ferr_ok;
	uint8_t hooks_in_use;
	volatile bool do_wait = false;
	fwaitq_waiter_t suspend_waiter;

	fwaitq_waiter_init(&suspend_waiter, fthread_suspend_timeout_suspend_waiter, (void*)&do_wait);

	if (!thread) {
		thread = fthread_current();
	}

	private_thread = (void*)thread;

	flock_spin_intsafe_lock(&thread->lock);

	hooks_in_use = private_thread->hooks_in_use;

	if (hooks_in_use == 0) {
		status = ferr_invalid_argument;
		goto out_locked;
	}

	prev_exec_state = fthread_state_execution_read_locked(thread);

	if (prev_exec_state == fthread_state_execution_dead || (thread->state & fthread_state_pending_death) != 0) {
		status = ferr_permanent_outage;
	} else if (prev_exec_state == fthread_state_execution_suspended || (thread->state & fthread_state_pending_suspend) != 0) {
		status = ferr_already_in_progress;
		if ((thread->state & fthread_state_pending_suspend) != 0 && wait) {
			do_wait = true;
		}
	} else {
		bool handled = false;

		thread->state |= fthread_state_pending_suspend;
		private_thread->pending_timeout_value = timeout_value;
		private_thread->pending_timeout_type = timeout_type;

		for (uint8_t slot = 0; slot < sizeof(private_thread->hooks) / sizeof(*private_thread->hooks); ++slot) {
			if ((hooks_in_use & (1 << slot)) == 0) {
				continue;
			}

			if (!private_thread->hooks[slot].suspend) {
				continue;
			}

			ferr_t hook_status = private_thread->hooks[slot].suspend(private_thread->hooks[slot].context, thread);

			if (hook_status == ferr_ok || hook_status == ferr_permanent_outage) {
				handled = true;

				// re-check the state
				if ((thread->state & fthread_state_pending_suspend) != 0 && wait) {
					// if we want to wait and the thread hasn't been suspended yet, we need to go ahead and register a waiter and wait
					do_wait = true;
				}
			}

			if (hook_status == ferr_permanent_outage) {
				break;
			}
		}

		if (!handled) {
			fpanic("No hooks were able to handle the thread suspension");
		}
	}

	if (do_wait && wait) {
		// register a waiter to be notified when the thread is finally suspended
		fwaitq_wait(&thread->suspend_wait, &suspend_waiter);
	}

out_locked:
	flock_spin_intsafe_unlock(&thread->lock);

	if (do_wait && wait) {
		// wait until the waiter notifies us that the thread was suspended
		while (do_wait) {
			// TODO: do something better than just spinning
		}
	}

	return status;
};

ferr_t fthread_suspend(fthread_t* thread, bool wait) {
	return fthread_suspend_timeout(thread, wait, 0, 0);
};

void fthread_suspend_self(void) {
	if (fthread_suspend(NULL, false) != ferr_ok) {
		fpanic("Failed to suspend own thread");
	}
};

ferr_t fthread_resume(fthread_t* thread) {
	// we don't accept `NULL` here because if you're suspended, you can't resume yourself. that's just not possible.
	fthread_private_t* private_thread = (void*)thread;
	fthread_state_execution_t prev_exec_state;
	ferr_t status = ferr_ok;
	uint8_t hooks_in_use;

	if (!thread) {
		return ferr_invalid_argument;
	}

	flock_spin_intsafe_lock(&thread->lock);

	hooks_in_use = private_thread->hooks_in_use;

	if (hooks_in_use == 0) {
		status = ferr_invalid_argument;
		goto out_locked;
	}

	prev_exec_state = fthread_state_execution_read_locked(thread);

	if (prev_exec_state == fthread_state_execution_dead || (thread->state & fthread_state_pending_death) != 0) {
		status = ferr_permanent_outage;
	} else if (prev_exec_state != fthread_state_execution_suspended && (thread->state & fthread_state_pending_suspend) == 0) {
		status = ferr_already_in_progress;
	} else {
		bool handled = false;

		thread->state &= ~fthread_state_pending_suspend;

		for (uint8_t slot = 0; slot < sizeof(private_thread->hooks) / sizeof(*private_thread->hooks); ++slot) {
			if ((hooks_in_use & (1 << slot)) == 0) {
				continue;
			}

			if (!private_thread->hooks[slot].resume) {
				continue;
			}

			ferr_t hook_status = private_thread->hooks[slot].resume(private_thread->hooks[slot].context, thread);

			if (hook_status == ferr_ok || hook_status == ferr_permanent_outage) {
				handled = true;
			}

			if (hook_status == ferr_permanent_outage) {
				break;
			}
		}

		if (!handled) {
			fpanic("No hooks were able to handle the thread resumption");
		}
	}

out_locked:
	flock_spin_intsafe_unlock(&thread->lock);

	return status;
};

ferr_t fthread_kill(fthread_t* thread) {
	fthread_private_t* private_thread;
	fthread_state_execution_t prev_exec_state;
	ferr_t status = ferr_ok;
	uint8_t hooks_in_use;

	if (!thread) {
		thread = fthread_current();
	}

	private_thread = (void*)thread;

	if (fthread_retain(thread) != ferr_ok) {
		return ferr_invalid_argument;
	}

	flock_spin_intsafe_lock(&thread->lock);

	hooks_in_use = private_thread->hooks_in_use;

	if (hooks_in_use == 0) {
		status = ferr_invalid_argument;
		goto out_locked;
	}

	prev_exec_state = fthread_state_execution_read_locked(thread);

	if (prev_exec_state == fthread_state_execution_dead || (thread->state & fthread_state_pending_death) != 0) {
		status = ferr_already_in_progress;
	} else {
		bool handled = false;

		thread->state |= fthread_state_pending_death;

		for (uint8_t slot = 0; slot < sizeof(private_thread->hooks) / sizeof(*private_thread->hooks); ++slot) {
			if ((hooks_in_use & (1 << slot)) == 0) {
				continue;
			}

			if (!private_thread->hooks[slot].kill) {
				continue;
			}

			ferr_t hook_status = private_thread->hooks[slot].kill(private_thread->hooks[slot].context, thread);

			if (hook_status == ferr_ok || hook_status == ferr_permanent_outage) {
				handled = true;
			}

			if (hook_status == ferr_permanent_outage) {
				break;
			}
		}

		if (!handled) {
			fpanic("No hooks were able to handle the thread assassination");
		}
	}

	flock_spin_intsafe_unlock(&thread->lock);

out_locked:
	fthread_release(thread);

	return status;
};

static void fthread_block_waiter(void* context) {
	volatile bool* do_wait = context;
	*do_wait = false;
};

ferr_t fthread_block(fthread_t* thread, bool wait) {
	fthread_private_t* private_thread;
	fthread_state_execution_t prev_exec_state;
	ferr_t status = ferr_ok;
	uint8_t hooks_in_use;
	volatile bool do_wait = false;
	fwaitq_waiter_t block_waiter;

	fwaitq_waiter_init(&block_waiter, fthread_block_waiter, (void*)&do_wait);

	if (!thread) {
		thread = fthread_current();
	}

	private_thread = (void*)thread;

	flock_spin_intsafe_lock(&thread->lock);

	hooks_in_use = private_thread->hooks_in_use;

	if (hooks_in_use == 0) {
		status = ferr_invalid_argument;
		goto out_locked;
	}

	prev_exec_state = fthread_state_execution_read_locked(thread);

	if (prev_exec_state == fthread_state_execution_dead || (thread->state & fthread_state_pending_death) != 0) {
		status = ferr_permanent_outage;
	} else if ((thread->state & (fthread_state_pending_block | fthread_state_blocked)) != 0) {
		++thread->block_count;
		if ((thread->state & fthread_state_pending_block) != 0 && wait) {
			do_wait = true;
		}
	} else {
		bool handled = false;

		thread->state |= fthread_state_pending_block;
		++thread->block_count;

		for (uint8_t slot = 0; slot < sizeof(private_thread->hooks) / sizeof(*private_thread->hooks); ++slot) {
			if ((hooks_in_use & (1 << slot)) == 0) {
				continue;
			}

			if (!private_thread->hooks[slot].block) {
				continue;
			}

			ferr_t hook_status = private_thread->hooks[slot].block(private_thread->hooks[slot].context, thread);

			if (hook_status == ferr_ok || hook_status == ferr_permanent_outage) {
				handled = true;

				// re-check the state
				if ((thread->state & fthread_state_pending_block) != 0 && wait) {
					// if we want to wait and the thread hasn't been blocked yet, we need to go ahead and register a waiter and wait
					do_wait = true;
				}
			}

			if (hook_status == ferr_permanent_outage) {
				break;
			}
		}

		if (!handled) {
			fpanic("No hooks were able to handle the thread block");
		}
	}

	if (do_wait && wait) {
		// register a waiter to be notified when the thread is finally blocked
		fwaitq_wait(&thread->block_wait, &block_waiter);
	}

out_locked:
	flock_spin_intsafe_unlock(&thread->lock);

	if (do_wait && wait) {
		// wait until the waiter notifies us that the thread was blocked
		while (do_wait) {
			// TODO: do something better than just spinning
		}
	}

	return status;
};

ferr_t fthread_unblock(fthread_t* thread) {
	// we don't accept `NULL` here because if you're blocked, you can't unblock yourself. that's just not possible.
	fthread_private_t* private_thread = (void*)thread;
	fthread_state_execution_t prev_exec_state;
	ferr_t status = ferr_ok;
	uint8_t hooks_in_use;

	if (!thread) {
		return ferr_invalid_argument;
	}

	flock_spin_intsafe_lock(&thread->lock);

	hooks_in_use = private_thread->hooks_in_use;

	if (hooks_in_use == 0) {
		status = ferr_invalid_argument;
		goto out_locked;
	}

	prev_exec_state = fthread_state_execution_read_locked(thread);

	if (prev_exec_state == fthread_state_execution_dead || (thread->state & fthread_state_pending_death) != 0) {
		status = ferr_permanent_outage;
	} else if ((thread->state & (fthread_state_pending_block | fthread_state_blocked)) == 0) {
		status = ferr_already_in_progress;
	} else {
		bool handled = false;

		--thread->block_count;
		if (thread->block_count > 0) {
			// don't actually unblock it until it reaches 0
			status = ferr_ok;
			goto out_locked;
		}

		thread->state &= ~fthread_state_pending_block;

		for (uint8_t slot = 0; slot < sizeof(private_thread->hooks) / sizeof(*private_thread->hooks); ++slot) {
			if ((hooks_in_use & (1 << slot)) == 0) {
				continue;
			}

			if (!private_thread->hooks[slot].unblock) {
				continue;
			}

			ferr_t hook_status = private_thread->hooks[slot].unblock(private_thread->hooks[slot].context, thread);

			if (hook_status == ferr_ok || hook_status == ferr_permanent_outage) {
				handled = true;
			}

			if (hook_status == ferr_permanent_outage) {
				break;
			}
		}

		if (!handled) {
			fpanic("No hooks were able to handle the thread unblock");
		}
	}

out_locked:
	flock_spin_intsafe_unlock(&thread->lock);

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
	uint8_t hooks_in_use;

	flock_spin_intsafe_lock(&thread->lock);
	hooks_in_use = private_thread->hooks_in_use;
	flock_spin_intsafe_unlock(&thread->lock);

	for (uint8_t slot = 0; slot < sizeof(private_thread->hooks) / sizeof(*private_thread->hooks); ++slot) {
		if ((hooks_in_use & (1 << slot)) == 0) {
			continue;
		}

		if (!private_thread->hooks[slot].interrupted) {
			continue;
		}

		ferr_t hook_status = private_thread->hooks[slot].interrupted(private_thread->hooks[slot].context, thread);

		if (hook_status == ferr_permanent_outage) {
			break;
		}
	}
};

void fthread_interrupt_end(fthread_t* thread) {
	fthread_private_t* private_thread = (void*)thread;
	uint8_t hooks_in_use;

	flock_spin_intsafe_lock(&thread->lock);
	hooks_in_use = private_thread->hooks_in_use;
	flock_spin_intsafe_unlock(&thread->lock);

	for (uint8_t slot = 0; slot < sizeof(private_thread->hooks) / sizeof(*private_thread->hooks); ++slot) {
		if ((hooks_in_use & (1 << slot)) == 0) {
			continue;
		}

		if (!private_thread->hooks[slot].ending_interrupt) {
			continue;
		}

		ferr_t hook_status = private_thread->hooks[slot].ending_interrupt(private_thread->hooks[slot].context, thread);

		if (hook_status == ferr_permanent_outage) {
			break;
		}
	}
};

static void fthread_died_worker(void* context) {
	fthread_t* thread = context;

	if ((thread->flags & fthread_flag_deallocate_stack_on_exit) != 0) {
		if (fpage_free_kernel(thread->stack_base, fpage_round_up_to_page_count(thread->stack_size)) != ferr_ok) {
			fpanic("Failed to free thread stack");
		}
	}

	fwaitq_wake_many(&thread->death_wait, SIZE_MAX);
};

void fthread_died(fthread_t* thread) {
	// this is fine even if the thread that's dying is a worker thread, because there's always going to be
	// at least one worker thread alive and available for the system to use
	fpanic_status(fwork_schedule_new(fthread_died_worker, thread, 0, NULL));
};

void fthread_suspended(fthread_t* thread) {
	fwaitq_wake_many(&thread->suspend_wait, SIZE_MAX);
};

void fthread_blocked(fthread_t* thread) {
	fwaitq_wake_many(&thread->block_wait, SIZE_MAX);
};

fthread_state_execution_t fthread_execution_state(fthread_t* thread) {
	fthread_state_execution_t result;
	flock_spin_intsafe_lock(&thread->lock);
	result = fthread_state_execution_read_locked(thread);
	flock_spin_intsafe_unlock(&thread->lock);
	return result;
};

static void wakeup_thread(void* data) {
	fthread_t* thread = data;
	// ignore the result. we don't care because:
	//   * if it was suspended, awesome; that's the optimal (and most common) case.
	//   * if it's already running, great; just do nothing.
	//   * if it's dead, great (although this case shouldn't happen).
	// any of the cases are fine with us.
	FERRO_WUR_IGNORE(fthread_resume(thread));
};

// TODO: move the 
#if FERRO_ARCH == FERRO_ARCH_x86_64
	#include <ferro/core/x86_64/per-cpu.private.h>
	#define FTHREAD_SAVED_CONTEXT_EXTRA_SIZE (FARCH_PER_CPU(xsave_area_size))
#else
	#define FTHREAD_SAVED_CONTEXT_EXTRA_SIZE 0
#endif

static void thread_invalid_instruction_handler(void* context) {
	fthread_t* thread = fthread_current();
	fthread_private_t* private_thread = (void*)thread;
	bool handled = false;
	uint8_t hooks_in_use;

	if (!thread) {
		goto out_fault;
	}

	if (fint_current_frame() != fint_root_frame(fint_current_frame())) {
		// we only handle faults for the current thread;
		// if this is a nested interrupt, the fault did not occur on the current thread
		goto out_fault;
	}

	flock_spin_intsafe_lock(&thread->lock);
	hooks_in_use = private_thread->hooks_in_use;
	flock_spin_intsafe_unlock(&thread->lock);

	for (uint8_t slot = 0; slot < sizeof(private_thread->hooks) / sizeof(*private_thread->hooks); ++slot) {
		if ((hooks_in_use & (1 << slot)) == 0) {
			continue;
		}

		if (!private_thread->hooks[slot].illegal_instruction) {
			continue;
		}

		ferr_t hook_status = private_thread->hooks[slot].illegal_instruction(private_thread->hooks[slot].context, thread);

		if (hook_status == ferr_ok || hook_status == ferr_permanent_outage) {
			handled = true;
		}

		if (hook_status == ferr_permanent_outage) {
			break;
		}
	}

	if (handled) {
		return;
	}

out_fault:
	fconsole_logf("invalid instruction; frame:\n");
	fint_log_frame(fint_current_frame());
	fint_trace_interrupted_stack(fint_current_frame());
	fpanic("invalid instruction");
};

static void thread_debug_handler(void* context) {
	fthread_t* thread = fthread_current();
	fthread_private_t* private_thread = (void*)thread;
	bool handled = false;
	uint8_t hooks_in_use;

	if (!thread) {
		goto out_ignore;
	}

	if (fint_current_frame() != fint_root_frame(fint_current_frame())) {
		// we only handle single steps for the current thread;
		// if this is a nested interrupt, the single step did not occur on the current thread
		goto out_ignore;
	}

	flock_spin_intsafe_lock(&thread->lock);
	hooks_in_use = private_thread->hooks_in_use;
	flock_spin_intsafe_unlock(&thread->lock);

	for (uint8_t slot = 0; slot < sizeof(private_thread->hooks) / sizeof(*private_thread->hooks); ++slot) {
		if ((hooks_in_use & (1 << slot)) == 0) {
			continue;
		}

		if (!private_thread->hooks[slot].debug_trap) {
			continue;
		}

		ferr_t hook_status = private_thread->hooks[slot].debug_trap(private_thread->hooks[slot].context, thread);

		if (hook_status == ferr_ok || hook_status == ferr_permanent_outage) {
			handled = true;
		}

		if (hook_status == ferr_permanent_outage) {
			break;
		}
	}

	if (handled) {
		return;
	}

out_ignore:
	return;
};

void fthread_init(void) {
	ferr_t status = ferr_ok;
	status |= fint_register_special_handler(fint_special_interrupt_invalid_instruction, thread_invalid_instruction_handler, NULL);
	status |= fint_register_special_handler(fint_special_interrupt_common_single_step, thread_debug_handler, NULL);
	status |= fint_register_special_handler(fint_special_interrupt_common_breakpoint, thread_debug_handler, NULL);
	status |= fint_register_special_handler(fint_special_interrupt_common_watchpoint, thread_debug_handler, NULL);
	if (status != ferr_ok) {
		// it's likely that the gdbstub subsystem already registered handlers for these.
		// in that case, register ourselves with the gdbstub subsystem.
		fpanic_status(fgdb_register_passthrough_handlers(thread_debug_handler, thread_debug_handler, thread_debug_handler));
	}
};

ferr_t fthread_new(fthread_initializer_f initializer, void* data, void* stack_base, size_t stack_size, fthread_flags_t flags, fthread_t** out_thread) {
	fthread_private_t* new_thread = NULL;
	bool release_stack_on_fail = false;
	fthread_saved_context_t* saved_context = NULL;

	if (!initializer || !out_thread) {
		return ferr_invalid_argument;
	}

	if (!stack_base) {
		if (fpage_allocate_kernel(fpage_round_up_to_page_count(stack_size), &stack_base, 0) != ferr_ok) {
			return ferr_temporary_outage;
		}

		release_stack_on_fail = true;
		flags |= fthread_flag_deallocate_stack_on_exit;
	}

	if (fmempool_allocate_advanced(sizeof(*saved_context) + FTHREAD_SAVED_CONTEXT_EXTRA_SIZE, fpage_round_up_to_alignment_power(64), UINT8_MAX, 0, NULL, (void*)&saved_context) != ferr_ok) {
		if (release_stack_on_fail) {
			if (fpage_free_kernel(stack_base, fpage_round_up_to_page_count(stack_size)) != ferr_ok) {
				fpanic("Failed to free thread stack");
			}
		}
		return ferr_temporary_outage;
	}

	if (fmempool_allocate(sizeof(fthread_private_t), NULL, (void**)&new_thread) != ferr_ok) {
		if (release_stack_on_fail) {
			if (fpage_free_kernel(stack_base, fpage_round_up_to_page_count(stack_size)) != ferr_ok) {
				fpanic("Failed to free thread stack");
			}
		}
		FERRO_WUR_IGNORE(fmempool_free(saved_context));
		return ferr_temporary_outage;
	}

	*out_thread = (void*)new_thread;

	// clear the thread
	simple_memset(new_thread, 0, sizeof(*new_thread));

	flock_spin_intsafe_init(&new_thread->thread.lock);

	frefcount_init(&new_thread->thread.reference_count);

	new_thread->thread.stack_base = stack_base;
	new_thread->thread.stack_size = stack_size;

	new_thread->thread.flags = flags;

	new_thread->timer_id = FTIMERS_ID_INVALID;

	new_thread->thread.id = FTHREAD_ID_INVALID;

	new_thread->thread.saved_context = saved_context;
	simple_memset(new_thread->thread.saved_context, 0, sizeof(*new_thread->thread.saved_context) + FTHREAD_SAVED_CONTEXT_EXTRA_SIZE);

	// the thread must start as suspended
	fthread_state_execution_write_locked(&new_thread->thread, fthread_state_execution_suspended);

	fwaitq_waiter_init(&new_thread->thread.wait_link, wakeup_thread, new_thread);

	farch_thread_init_info(&new_thread->thread, initializer, data);

	return ferr_ok;
};

static void timeout_callback(void* data) {
	fthread_t* thread = data;
	fthread_private_t* private_thread = data;

	flock_spin_intsafe_lock(&thread->lock);
	private_thread->timer_id = FTIMERS_ID_INVALID;
	flock_spin_intsafe_unlock(&thread->lock);

	FERRO_WUR_IGNORE(fthread_resume(thread));
};

ferr_t fthread_wait_timeout(fthread_t* thread, fwaitq_t* waitq, uint64_t timeout_value, fthread_timeout_type_t timeout_type) {
	fthread_private_t* private_thread;
	fthread_state_execution_t prev_exec_state;
	ferr_t status = ferr_ok;
	uint8_t hooks_in_use;

	if (!thread) {
		thread = fthread_current();
	}

	private_thread = (void*)thread;

	flock_spin_intsafe_lock(&thread->lock);

	hooks_in_use = private_thread->hooks_in_use;

	if (hooks_in_use == 0) {
		status = ferr_invalid_argument;
		goto out_locked;
	}

	prev_exec_state = fthread_state_execution_read_locked(thread);

	if (prev_exec_state == fthread_state_execution_dead || (thread->state & fthread_state_pending_death) != 0) {
		status = ferr_permanent_outage;
	} else if (prev_exec_state == fthread_state_execution_suspended) {
		// we were already suspended; we can add ourselves onto the waitq's waiting list right now

		// if we already had a waitq, we need to remove ourselves from its waiting list
		if (thread->waitq) {
			// once we're suspended, we can't be holding the waitq lock anymore, so there's no need to check
			fwaitq_lock(thread->waitq);
			fwaitq_remove_locked(thread->waitq, &thread->wait_link);
			fwaitq_unlock(thread->waitq);
			thread->waitq = NULL;
		}

		// now lets add ourselves to the new waitq's waiting list
		fwaitq_lock(waitq);
		fwaitq_add_locked(waitq, &thread->wait_link);
		fwaitq_unlock(waitq);
		thread->waitq = waitq;

		if (private_thread->timer_id != FTIMERS_ID_INVALID) {
			FERRO_WUR_IGNORE(ftimers_cancel(private_thread->timer_id));
		}

		private_thread->timer_id = FTIMERS_ID_INVALID;
		private_thread->pending_timeout_value = timeout_value;
		private_thread->pending_timeout_type = timeout_type;

		if (private_thread->pending_timeout_value > 0) {
			if (private_thread->pending_timeout_type == fthread_timeout_type_ns_relative) {
				if (ftimers_oneshot_blocking(private_thread->pending_timeout_value, timeout_callback, thread, &private_thread->timer_id) != ferr_ok) {
					fpanic("Failed to set up thread wakeup timeout");
				}
			} else {
				fpanic("Unsupported timeout type: %d", private_thread->pending_timeout_type);
			}
		}
		private_thread->pending_timeout_type = 0;
		private_thread->pending_timeout_value = 0;
	} else if ((thread->state & fthread_state_pending_suspend) != 0) {
		// we're not suspended yet; we can just overwrite the old pending waitq with a new one
		if (thread->waitq && (thread->state & fthread_state_holding_waitq_lock) != 0) {
			fwaitq_unlock(thread->waitq);
		}
		thread->state &= ~fthread_state_holding_waitq_lock;
		thread->waitq = waitq;
		private_thread->pending_timeout_value = timeout_value;
		private_thread->pending_timeout_type = timeout_type;
	} else {
		bool handled = false;

		// otherwise, we need to perform the same operation as fthread_suspend(), but with a pending waitq to wait on
		thread->state |= fthread_state_pending_suspend;
		thread->waitq = waitq;
		private_thread->pending_timeout_value = timeout_value;
		private_thread->pending_timeout_type = timeout_type;

		for (uint8_t slot = 0; slot < sizeof(private_thread->hooks) / sizeof(*private_thread->hooks); ++slot) {
			if ((hooks_in_use & (1 << slot)) == 0) {
				continue;
			}

			if (!private_thread->hooks[slot].suspend) {
				continue;
			}

			ferr_t hook_status = private_thread->hooks[slot].suspend(private_thread->hooks[slot].context, thread);

			if (hook_status == ferr_ok || hook_status == ferr_permanent_outage) {
				handled = true;
			}

			if (hook_status == ferr_permanent_outage) {
				break;
			}
		}

		if (!handled) {
			fpanic("No hooks were able to handle the thread suspension");
		}
	}

out_locked:
	flock_spin_intsafe_unlock(&thread->lock);

	return status;
};

ferr_t fthread_wait(fthread_t* thread, fwaitq_t* waitq) {
	return fthread_wait_timeout(thread, waitq, 0, 0);
};

ferr_t fthread_wait_timeout_locked(fthread_t* thread, fwaitq_t* waitq, uint64_t timeout_value, fthread_timeout_type_t timeout_type) {
	// unfortunately, we have to duplicate much of fthread_wait() because it's not as simple as having fthread_wait() lock the waitq and then call us because the behavior is slightly different.
	// for example, in the already-suspended case, we want to avoid deadlock if possible.
	// this is possible for fthread_wait(), because it doesn't lock the new waitq until after it's done with the old waitq,
	// but not for us, because we don't want to drop the new waitq's lock until we're completely done with it.

	fthread_private_t* private_thread;
	fthread_state_execution_t prev_exec_state;
	ferr_t status = ferr_ok;
	uint8_t hooks_in_use;

	if (!thread) {
		thread = fthread_current();
	}

	private_thread = (void*)thread;

	flock_spin_intsafe_lock(&thread->lock);

	hooks_in_use = private_thread->hooks_in_use;

	if (hooks_in_use == 0) {
		status = ferr_invalid_argument;
		goto out_locked;
	}

	prev_exec_state = fthread_state_execution_read_locked(thread);

	if (prev_exec_state == fthread_state_execution_dead || (thread->state & fthread_state_pending_death) != 0) {
		status = ferr_permanent_outage;
	} else if (prev_exec_state == fthread_state_execution_suspended) {
		// we were already suspended; we can add ourselves onto the waitq's waiting list right now

		// if we already had a waitq, we need to remove ourselves from its waiting list
		if (thread->waitq) {
			// once we're suspended, we can't be holding the waitq lock anymore, so there's no need to check
			fwaitq_lock(thread->waitq);
			fwaitq_remove_locked(thread->waitq, &thread->wait_link);
			fwaitq_unlock(thread->waitq);
			thread->waitq = NULL;
		}

		// now lets add ourselves to the new waitq's waiting list
		fwaitq_add_locked(waitq, &thread->wait_link);
		fwaitq_unlock(waitq);
		thread->waitq = waitq;

		if (private_thread->timer_id != FTIMERS_ID_INVALID) {
			FERRO_WUR_IGNORE(ftimers_cancel(private_thread->timer_id));
		}

		private_thread->timer_id = FTIMERS_ID_INVALID;
		private_thread->pending_timeout_value = timeout_value;
		private_thread->pending_timeout_type = timeout_type;

		if (private_thread->pending_timeout_value > 0) {
			if (private_thread->pending_timeout_type == fthread_timeout_type_ns_relative) {
				if (ftimers_oneshot_blocking(private_thread->pending_timeout_value, timeout_callback, thread, &private_thread->timer_id) != ferr_ok) {
					fpanic("Failed to set up thread wakeup timeout");
				}
			} else {
				fpanic("Unsupported timeout type: %d", private_thread->pending_timeout_type);
			}
		}
		private_thread->pending_timeout_type = 0;
		private_thread->pending_timeout_value = 0;
	} else if ((thread->state & fthread_state_pending_suspend) != 0) {
		// we're not suspended yet; we can just overwrite the old pending waitq with a new one
		if (thread->waitq && (thread->state & fthread_state_holding_waitq_lock) != 0) {
			fwaitq_unlock(thread->waitq);
		}
		thread->state |= fthread_state_holding_waitq_lock;
		thread->waitq = waitq;
		private_thread->pending_timeout_value = timeout_value;
		private_thread->pending_timeout_type = timeout_type;
	} else {
		bool handled = false;

		// otherwise, we need to perform the same operation as fthread_suspend(), but with a pending waitq to wait on
		thread->state |= fthread_state_pending_suspend | fthread_state_holding_waitq_lock;
		thread->waitq = waitq;
		private_thread->pending_timeout_value = timeout_value;
		private_thread->pending_timeout_type = timeout_type;

		for (uint8_t slot = 0; slot < sizeof(private_thread->hooks) / sizeof(*private_thread->hooks); ++slot) {
			if ((hooks_in_use & (1 << slot)) == 0) {
				continue;
			}

			if (!private_thread->hooks[slot].suspend) {
				continue;
			}

			ferr_t hook_status = private_thread->hooks[slot].suspend(private_thread->hooks[slot].context, thread);

			if (hook_status == ferr_ok || hook_status == ferr_permanent_outage) {
				handled = true;
			}

			if (hook_status == ferr_permanent_outage) {
				break;
			}
		}

		if (!handled) {
			fpanic("No hooks were able to handle the thread suspension");
		}
	}

out_locked:
	flock_spin_intsafe_unlock(&thread->lock);

	return status;
};

ferr_t fthread_wait_locked(fthread_t* thread, fwaitq_t* waitq) {
	return fthread_wait_timeout_locked(thread, waitq, 0, 0);
};

uint8_t fthread_register_hook(fthread_t* thread, uint64_t owner_id, void* context, const fthread_hook_callbacks_t* callbacks) {
	fthread_private_t* private_thread = (void*)thread;

	flock_spin_intsafe_lock(&thread->lock);

	for (uint8_t slot = 0; slot < sizeof(private_thread->hooks) / sizeof(*private_thread->hooks); ++slot) {
		if ((private_thread->hooks_in_use & (1 << slot)) != 0) {
			continue;
		}

		private_thread->hooks_in_use |= 1 << slot;

		private_thread->hooks[slot].context = context;
		private_thread->hooks[slot].owner_id = owner_id;

		private_thread->hooks[slot].suspend = callbacks->suspend;
		private_thread->hooks[slot].resume = callbacks->resume;
		private_thread->hooks[slot].kill = callbacks->kill;
		private_thread->hooks[slot].block = callbacks->block;
		private_thread->hooks[slot].unblock = callbacks->unblock;
		private_thread->hooks[slot].interrupted = callbacks->interrupted;
		private_thread->hooks[slot].ending_interrupt = callbacks->ending_interrupt;
		private_thread->hooks[slot].bus_error = callbacks->bus_error;
		private_thread->hooks[slot].page_fault = callbacks->page_fault;
		private_thread->hooks[slot].floating_point_exception = callbacks->floating_point_exception;
		private_thread->hooks[slot].illegal_instruction = callbacks->illegal_instruction;
		private_thread->hooks[slot].debug_trap = callbacks->debug_trap;
		private_thread->hooks[slot].division_by_zero = callbacks->division_by_zero;

		flock_spin_intsafe_unlock(&thread->lock);
		return slot;
	}

	flock_spin_intsafe_unlock(&thread->lock);
	return UINT8_MAX;
};

uint8_t fthread_find_hook(fthread_t* thread, uint64_t owner_id) {
	fthread_private_t* private_thread = (void*)thread;

	if (!thread) {
		return UINT8_MAX;
	}

	flock_spin_intsafe_lock(&thread->lock);

	for (uint8_t slot = 0; slot < sizeof(private_thread->hooks) / sizeof(*private_thread->hooks); ++slot) {
		if ((private_thread->hooks_in_use & (1 << slot)) == 0) {
			continue;
		}

		if (private_thread->hooks[slot].owner_id != owner_id) {
			continue;
		}

		flock_spin_intsafe_unlock(&thread->lock);
		return slot;
	}

	flock_spin_intsafe_unlock(&thread->lock);
	return UINT8_MAX;
};

void fthread_mark_interrupted(fthread_t* thread) {
	flock_spin_intsafe_lock(&thread->lock);
	thread->state |= fthread_state_interrupted;
	flock_spin_intsafe_unlock(&thread->lock);
};

void fthread_unmark_interrupted(fthread_t* thread) {
	flock_spin_intsafe_lock(&thread->lock);
	thread->state &= ~fthread_state_interrupted;
	flock_spin_intsafe_unlock(&thread->lock);
};

bool fthread_marked_interrupted(fthread_t* thread) {
	bool result = false;
	flock_spin_intsafe_lock(&thread->lock);
	result = (thread->state & fthread_state_interrupted) != 0;
	flock_spin_intsafe_unlock(&thread->lock);
	return result;
};
