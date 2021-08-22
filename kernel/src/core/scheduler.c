#include <ferro/core/scheduler.private.h>
#include <ferro/core/timers.h>
#include <ferro/core/console.h>
#include <ferro/core/panic.h>
#include <ferro/core/entry.h>
#include <ferro/core/cpu.h>
#include <ferro/core/mempool.h>
#include <ferro/core/threads.private.h>

#include <stdatomic.h>

// TODO: if we only have one thread in a queue, we should just let it run without a timer (i.e. without preemption).
//       we should only preempt it once we have another thread available to run on that queue.

/**
 * How many nanoseconds to let a thread to run before preempting it.
 * The current value is `500us` in nanoseconds.
 */
#define SLICE_NS 500000ULL

static void manager_kill(fthread_t* thread);
static void manager_suspend(fthread_t* thread);
static void manager_resume(fthread_t* thread);
static void manager_interrupted(fthread_t* thread);
static void manager_ending_interrupt(fthread_t* thread);

static fsched_info_t** infos = NULL;
static size_t info_count = 0;

// the suspension circular queue is shared among all CPUs.
// it's where threads that get suspended are placed. when they're resumed, they can be assigned to any CPU.
static fsched_info_t suspended = {0};

// these are scheduled when a CPU has nothing else to do.
// note that these aren't actually scheduled; they're invisible to queues.
// they're just context-switched to and from.
static fthread_t** idle_threads = NULL;

static fthread_manager_t scheduler_thread_manager = {
	.kill = manager_kill,
	.suspend = manager_suspend,
	.resume = manager_resume,
	.interrupted = manager_interrupted,
	.ending_interrupt = manager_ending_interrupt,
};

fsched_info_t* fsched_per_cpu_info(void) {
	return infos[fcpu_id()];
};

// returns with the queue in the same lock state as on entry
static void remove_from_queue(fthread_t* thread, bool queue_is_locked) {
	fthread_private_t* private_thread = (void*)thread;
	fsched_info_t* old_queue = private_thread->manager_private;

	if (!thread->prev || !thread->next || !old_queue) {
		if (thread->prev || thread->next || old_queue) {
			fpanic("Thread information structure inconsistency");
		}
		return;
	}

	if (!queue_is_locked) {
		flock_spin_intsafe_lock(&old_queue->lock);
	}

	if (old_queue->head == thread && old_queue->tail == thread) {
		old_queue->head = NULL;
		old_queue->tail = NULL;
	} else {
		if (old_queue->head == thread) {
			old_queue->head = thread->next;
		} else if (old_queue->tail == thread) {
			old_queue->tail = thread->prev;
		}

		thread->next->prev = thread->prev;
		thread->prev->next = thread->next;
	}

	--old_queue->count;

	thread->next = NULL;
	thread->prev = NULL;
	private_thread->manager_private = NULL;

	if (!queue_is_locked) {
		flock_spin_intsafe_unlock(&old_queue->lock);
	}
};

// returns with the queue in the same lock state as on entry
static void add_to_queue(fthread_t* thread, fsched_info_t* new_queue, bool new_queue_is_locked) {
	fthread_private_t* private_thread = (void*)thread;
	fsched_info_t* old_queue = private_thread->manager_private;

	if (thread->prev || thread->next || old_queue) {
		if (!thread->prev || !thread->next || !old_queue) {
			fpanic("Thread information structure inconsistency");
		}

		fpanic("Thread should first be removed from old queue before inserting into new one");
	}

	if (!new_queue_is_locked) {
		flock_spin_intsafe_lock(&new_queue->lock);
	}

	if (!new_queue->head && !new_queue->tail) {
		thread->prev = thread;
		thread->next = thread;
		new_queue->head = thread;
		new_queue->tail = thread;
	} else {
		thread->prev = new_queue->tail;
		thread->next = new_queue->tail->next;
		new_queue->tail->next->prev = thread;
		new_queue->tail->next = thread;
		new_queue->tail = thread;
	}

	private_thread->manager_private = new_queue;

	++new_queue->count;

	if (!new_queue_is_locked) {
		flock_spin_intsafe_unlock(&new_queue->lock);
	}
};

static void rotate_queue_forward(fsched_info_t* queue, bool queue_is_locked) {
	if (!queue_is_locked) {
		flock_spin_intsafe_lock(&queue->lock);
	}

	queue->tail = queue->head;
	queue->head = queue->head->next;

	if (!queue_is_locked) {
		flock_spin_intsafe_unlock(&queue->lock);
	}
};

// this is guaranteed to be called from within an interrupt context
static void timed_context_switch(void* data) {
	// we'll take care of pending deaths or suspensions later, when we're about to return from the interrupt
	fsched_info_t* queue = fsched_per_cpu_info();
	fthread_t* idle_thread = idle_threads[fcpu_id()];
	fthread_t* old_thread = fthread_current();

	flock_spin_intsafe_lock(&queue->lock);

	fsched_per_cpu_info()->last_timer_id = UINTPTR_MAX;

	// this should be impossible, but just in case
	if (queue->count == 0 && old_thread != idle_thread) {
		fsched_switch(NULL, idle_thread);
	} else if (queue->count > 1) {
		fthread_t* new_thread;

		flock_spin_intsafe_lock(&old_thread->lock);
		if (fthread_state_execution_read_locked(old_thread) == fthread_state_execution_interrupted) {
			// only if it was previously the active thread does it need to be rotated out
			rotate_queue_forward(queue, true);
		} else if (queue->head != old_thread) {
			fpanic("Scheduler state inconsistency (expected new thread to equal old thread)");
		}
		new_thread = queue->head;
		fthread_state_execution_write_locked(old_thread, fthread_state_execution_not_running);
		flock_spin_intsafe_unlock(&old_thread->lock);

		flock_spin_intsafe_lock(&new_thread->lock);
		fthread_state_execution_write_locked(new_thread, fthread_state_execution_interrupted);
		flock_spin_intsafe_unlock(&new_thread->lock);

		// only switch if the threads are different.
		// this is because when the threads are the same, it means the thread wasn't the previously active one and has already been switched.
		if (old_thread != new_thread) {
			fsched_switch(old_thread, new_thread);
		}
	} else {
		// whether this is the idle thread or the only thread in queue, we want to re-arm the timer.
		//
		// switching to the same thread arms the timer on return.
		fsched_switch(old_thread, old_thread);
	}

	flock_spin_intsafe_unlock(&queue->lock);
};

void fsched_arm_timer(void) {
	//static atomic_size_t counter = 0;
	//fconsole_logf("arming preemption timer %zu\n", counter++);
	if (ftimers_oneshot_blocking(SLICE_NS, timed_context_switch, NULL, &fsched_per_cpu_info()->last_timer_id) != ferr_ok) {
		fpanic("Failed to setup preemption timer");
	}
};

void fsched_disarm_timer(void) {
	fint_disable();
	if (fsched_per_cpu_info()->last_timer_id != UINTPTR_MAX) {
		ferr_t status = ftimers_cancel(fsched_per_cpu_info()->last_timer_id);
		// ignore the status
	}
	fint_enable();
};

static void scheduler_idle(void* data) {
	while (true) {
		fentry_idle();
	}
};

FERRO_NO_RETURN void fsched_init(fthread_t* thread) {
	fthread_private_t* private_thread = (void*)thread;
	fsched_info_t* this_info = NULL;

	farch_sched_init();

	info_count = fcpu_count();

	if (fmempool_allocate(sizeof(fsched_info_t*) * info_count, NULL, (void*)&infos) != ferr_ok) {
		fpanic("Failed to allocate scheduler information pointer array");
	}

	if (fmempool_allocate(sizeof(fthread_t*) * info_count, NULL, (void*)&idle_threads) != ferr_ok) {
		fpanic("Failed to allocate idle thread pointer array");
	}

	for (size_t i = 0; i < info_count; ++i) {
		if (fmempool_allocate(sizeof(fsched_info_t), NULL, (void*)&infos[i]) != ferr_ok) {
			fpanic("Failed to allocate scheduler information structure for CPU %lu", i);
		}

		if (fthread_new(scheduler_idle, NULL, NULL, 1024, 0, &idle_threads[i]) != ferr_ok) {
			fpanic("Failed to create idle thread for CPU %lu", i);
		}

		flock_spin_intsafe_init(&infos[i]->lock);
		infos[i]->head = NULL;
		infos[i]->tail = NULL;
		infos[i]->count = 0;
		infos[i]->last_timer_id = UINTPTR_MAX;
	}

	this_info = fsched_per_cpu_info();

	flock_spin_intsafe_lock(&thread->lock);

	thread->prev = thread;
	thread->next = thread;
	private_thread->manager = &scheduler_thread_manager;
	private_thread->manager_private = this_info;

	thread->state &= ~fthread_state_pending_suspend;
	fthread_state_execution_write_locked(thread, fthread_state_execution_running);

	this_info->head = thread;
	this_info->tail = thread;
	++this_info->count;

	flock_spin_intsafe_unlock(&thread->lock);

	// this will also arm the timer
	fsched_bootstrap(thread);
};

// must hold NO locks
// returns a scheduler information structure with its lock held
static fsched_info_t* find_lightest_load(void) {
	fsched_info_t* result = NULL;
	for (size_t i = 0; i < info_count; ++i) {
		size_t prev_count = result ? result->count : SIZE_MAX;
		if (result) {
			flock_spin_intsafe_unlock(&result->lock);
		}
		// yes, dropping the previous one's lock before acquire this one's lock means the count might've changed.
		// however, if we hold the lock, we can run into deadlocks; so let's prefer to be a little bit inaccurate rather than frozen.
		flock_spin_intsafe_lock(&infos[i]->lock);
		if (prev_count > infos[i]->count) {
			result = infos[i];
		}
	}
	return result;
};

static void manager_kill(fthread_t* thread) {
	fthread_private_t* private_thread = (void*)thread;
	fthread_state_execution_t prev_exec_state = fthread_state_execution_read_locked(thread);
	fsched_info_t* old_queue = private_thread->manager_private;

	// at this point, the threads subsystem has already ensured that:
	//   * the thread is not already dead.
	//   * the pending death bit has been set.

	if (prev_exec_state != fthread_state_execution_running && prev_exec_state != fthread_state_execution_interrupted) {
		// if it's not running, that's wonderful! our job is much easier.
		// note that the thread currently being interrupted is an issue because we don't know if it's the one being switched out or the one being switched in.
		fthread_state_execution_write_locked(thread, fthread_state_execution_dead);
		thread->state &= ~fthread_state_pending_death;
		remove_from_queue(thread, false);
		return;
	}

	// otherwise, it's currently running, so we'll have to ask our arch-specific code to stop it immediately

	// we don't want to be interrupted by the timer if it's for our current thread
	fint_disable();

	// unlock it for the call
	flock_spin_intsafe_unlock(&thread->lock);

	if (thread == fthread_current()) {
		// if it's the current thread, we're not returning, so we need to release the extra reference that `fthread_kill` acquired
		fthread_release(thread);
	}

	// this does not return if `thread == fthread_current()`
	fsched_preempt_thread(thread);

	// it might seem like the thread might be fully released here, but actually no.
	// `fthread_kill` retains the thread before calling us and then releases it afterwards.

	// and relock it for the threads subsystem
	flock_spin_intsafe_lock(&thread->lock);

	fint_enable();

	// that's it; once the thread returns to the context switcher, it should see that it's dying and finish the job
};

static void manager_suspend(fthread_t* thread) {
	fthread_private_t* private_thread = (void*)thread;
	fthread_state_execution_t prev_exec_state = fthread_state_execution_read_locked(thread);
	fsched_info_t* old_queue = private_thread->manager_private;

	// at this point, the threads subsystem has already ensured that:
	//   * the thread is neither dead nor dying.
	//   * the thread is neither suspended nor pending suspension.
	//   * the pending suspension bit has been set.

	// if it's not currently running, we can take care of it right now
	if (prev_exec_state != fthread_state_execution_running && prev_exec_state != fthread_state_execution_interrupted) {
		remove_from_queue(thread, false);

		// the suspension is no longer pending; it's now fully suspended
		fthread_state_execution_write_locked(thread, fthread_state_execution_suspended);
		thread->state &= ~fthread_state_pending_suspend;

		add_to_queue(thread, &suspended, false);
	}

	// we don't want to be interrupted by the timer if it's for our current thread
	fint_disable();

	// unlock it for the call
	flock_spin_intsafe_unlock(&thread->lock);

	fsched_preempt_thread(thread);

	// and relock it for the threads subsystem
	flock_spin_intsafe_lock(&thread->lock);

	fint_enable();

	// that's it; once the thread returns to the context switcher, it should see that it's pending suspension and finish the job
};

static void manager_resume(fthread_t* thread) {
	fthread_private_t* private_thread = (void*)thread;
	fthread_state_execution_t prev_exec_state = fthread_state_execution_read_locked(thread);
	fsched_info_t* old_queue = private_thread->manager_private;
	fsched_info_t* new_queue = NULL;

	// at this point, the threads subsystem has already ensured that:
	//   * the thread is neither dead nor dying.
	//   * the thread is either suspended or pending suspension.
	//   * if it was pending suspension, the pending suspension bit has been cleared.

	if (prev_exec_state != fthread_state_execution_suspended) {
		// if it's not currently suspended, it's already scheduled on a CPU.
		// in that case, clearing the pending suspension is enough to keep it running,
		// which the threads subsystem already does for us.
		return;
	}

	remove_from_queue(thread, false);

	fthread_state_execution_write_locked(thread, fthread_state_execution_not_running);

	// just as a note, here there are no chances for a deadlock.
	// at this point, the thread doesn't belong to queue, so we're the only ones that could possibly want to hold its lock.
	// we want the destination queue's lock, but whoever's holding it can't want our thread's lock until we after insert it.

	new_queue = find_lightest_load();
	if (!new_queue) {
		fpanic("Failed to find CPU with lightest load (this is impossible)");
	}

	add_to_queue(thread, new_queue, true);

	// unlock the queue that was locked by `find_lightest_load`
	flock_spin_intsafe_unlock(&new_queue->lock);
};

static fthread_t* clear_pending_death_or_suspension(fthread_t* thread) {
	fthread_private_t* private_thread = (void*)thread;
	fsched_info_t* queue = fsched_per_cpu_info();

	flock_spin_intsafe_lock(&queue->lock);

	// we should only have at most a single thread waiting for death or suspension, and it should only be the active thread.
	// all other threads aren't running, so when they're asked to be killed or suspended, they can do it immediately.
	if ((thread->state & (fthread_state_pending_death | fthread_state_pending_suspend)) != 0) {
		bool needs_to_suspend = false;
		fthread_t* new_thread = thread->next;

		if (private_thread->manager_private != queue) {
			fpanic("Thread information inconsistency (dying thread's queue is not current CPU's queue)");
		}

		if (new_thread == thread) {
			// that means we've reached the end of this queue; the new thread will instead be the idle thread for this CPU
			new_thread = idle_threads[fcpu_id()];
		}

		// save the thread's context and load the context for the new thread
		fsched_switch(thread, new_thread);

		// mark it as dead or suspended (depending on what we want)
		if ((thread->state & fthread_state_pending_death) != 0) {
			fthread_state_execution_write_locked(thread, fthread_state_execution_dead);
		} else {
			needs_to_suspend = true;
			fthread_state_execution_write_locked(thread, fthread_state_execution_suspended);
		}

		// clear the pending status(es) and remove it from the queue
		thread->state &= ~(fthread_state_pending_death | fthread_state_pending_suspend);
		remove_from_queue(thread, true);

		// if it needs to be suspended, it needs to be added to the suspension queue
		if (needs_to_suspend) {
			add_to_queue(thread, &suspended, false);
			flock_spin_intsafe_unlock(&thread->lock);
		} else {
			// drop the lock now; everyone else will see the thread is dead and not use it for further execution
			flock_spin_intsafe_unlock(&thread->lock);
			fthread_died(thread);
			fthread_release(thread);
		}

		thread = new_thread;
		private_thread = (void*)thread;
		// the queue should still be the same

		flock_spin_intsafe_lock(&thread->lock);
	}

	flock_spin_intsafe_unlock(&queue->lock);

	return thread;
};

static void manager_interrupted(fthread_t* thread) {
	flock_spin_intsafe_lock(&thread->lock);
	thread = clear_pending_death_or_suspension(thread);
	fthread_state_execution_write_locked(thread, fthread_state_execution_interrupted);
	flock_spin_intsafe_unlock(&thread->lock);
};

static void manager_ending_interrupt(fthread_t* thread) {
	// we actually can't have any more threads to clear due to death or suspension right here
	// because `kill` and `suspend` immediately remove the threads if they're not running
	// (and being interrupted counts as not running)
	//thread = clear_pending_death_or_suspension(thread);

	flock_spin_intsafe_lock(&thread->lock);
	fthread_state_execution_write_locked(thread, fthread_state_execution_running);
	flock_spin_intsafe_unlock(&thread->lock);
};

ferr_t fsched_manage(fthread_t* thread) {
	fthread_private_t* private_thread = (void*)thread;
	fthread_state_execution_t prev_exec_state;
	ferr_t status = ferr_ok;

	if (!thread) {
		status = ferr_invalid_argument;
		goto out_unlocked;
	}

	if (fthread_retain(thread) != ferr_ok) {
		// it was fully released before we managed to retain it
		status = ferr_invalid_argument;
		goto out_unlocked;
	}

	flock_spin_intsafe_lock(&thread->lock);

	prev_exec_state = fthread_state_execution_read_locked(thread);

	if (prev_exec_state == fthread_state_execution_dead || (thread->state & fthread_state_pending_death) != 0) {
		// well, we can't manage a dead thread
		status = ferr_invalid_argument;
		goto out;
	}

	// we now have to set everything on the thread needed to mark it as suspended

	thread->prev = NULL;
	thread->next = NULL;
	private_thread->manager = &scheduler_thread_manager;

	fthread_state_execution_write_locked(thread, fthread_state_execution_suspended);
	thread->state &= ~fthread_state_pending_suspend;

	add_to_queue(thread, &suspended, false);

out:
	flock_spin_intsafe_unlock(&thread->lock);

	if (status != ferr_ok) {
		fthread_release(thread);
	}

out_unlocked:
	return status;
};
