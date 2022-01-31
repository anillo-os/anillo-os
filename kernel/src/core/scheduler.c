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
 * A basic scheduler.
 */

#include <ferro/core/scheduler.private.h>
#include <ferro/core/timers.h>
#include <ferro/core/console.h>
#include <ferro/core/panic.h>
#include <ferro/core/entry.h>
#include <ferro/core/cpu.h>
#include <ferro/core/mempool.h>
#include <ferro/core/threads.private.h>
#include <ferro/core/waitq.private.h>
#include <ferro/core/interrupts.h>

#include <stdatomic.h>

// TODO: if we only have one thread in a queue, we should just let it run without a timer (i.e. without preemption).
//       we should only preempt it once we have another thread available to run on that queue.

/**
 * How many nanoseconds to let a thread to run before preempting it.
 * The current value is `500us` in nanoseconds.
 */
#define SLICE_NS 500000ULL

#define IDLE_THREAD_STACK_SIZE (4ULL * 1024)

// 5CEDUL -> SCEDUL -> Schedule
#define SCHEDULER_HOOK_OWNER_ID (0x5CEDUL)

static ferr_t manager_kill(void* context, fthread_t* thread);
static ferr_t manager_suspend(void* context, fthread_t* thread);
static ferr_t manager_resume(void* context, fthread_t* thread);
static ferr_t manager_interrupted(void* context, fthread_t* thread);
static ferr_t manager_ending_interrupt(void* context, fthread_t* thread);

fsched_info_t** fsched_infos = NULL;
size_t fsched_info_count = 0;
fsched_info_t fsched_suspended = {0};

// these are scheduled when a CPU has nothing else to do.
// note that these aren't actually scheduled; they're invisible to queues.
// they're just context-switched to and from.
static fthread_t** idle_threads = NULL;

static _Atomic fthread_id_t next_id = 0;

static fthread_t* global_thread_list = NULL;

/**
 * Protects ::global_thread_list.
 *
 * Yes, I know, this is terrible, but I'm feeling lazy right now and can't think any better alternative.
 * An R/W lock would probably be better here, once I get around to implementing those.
 */
static flock_spin_intsafe_t global_thread_lock = FLOCK_SPIN_INTSAFE_INIT;

static void global_thread_list_add(fthread_t* thread) {
	fthread_private_t* private_thread = (void*)thread;
	fsched_thread_private_t* sched_private = private_thread->hooks[0].context;

	flock_spin_intsafe_lock(&global_thread_lock);

	sched_private->global_next = global_thread_list;
	sched_private->global_prev = &global_thread_list;

	if (sched_private->global_next) {
		fthread_private_t* next_private_thread = (void*)sched_private->global_next;
		fsched_thread_private_t* next_sched_private = next_private_thread->hooks[0].context;

		next_sched_private->global_prev = &sched_private->global_next;
	}

	*sched_private->global_prev = thread;

	flock_spin_intsafe_unlock(&global_thread_lock);
};

static void global_thread_list_remove(fthread_t* thread) {
	fthread_private_t* private_thread = (void*)thread;
	fsched_thread_private_t* sched_private = private_thread->hooks[0].context;

	flock_spin_intsafe_lock(&global_thread_lock);

	*sched_private->global_prev = sched_private->global_next;

	if (sched_private->global_next) {
		fthread_private_t* next_private_thread = (void*)sched_private->global_next;
		fsched_thread_private_t* next_sched_private = next_private_thread->hooks[0].context;

		next_sched_private->global_prev = sched_private->global_prev;
	}

	flock_spin_intsafe_unlock(&global_thread_lock);
};

FERRO_ALWAYS_INLINE fthread_id_t get_next_id(void) {
	fthread_id_t result;

retry:
	result = next_id++;
	if (result == FTHREAD_ID_INVALID) {
		goto retry;
	}

	return result;
};

fsched_info_t* fsched_per_cpu_info(void) {
	return fsched_infos[fcpu_id()];
};

// returns with the queue in the same lock state as on entry
static void remove_from_queue(fthread_t* thread, bool queue_is_locked) {
	fthread_private_t* private_thread = (void*)thread;
	fsched_thread_private_t* sched_private = private_thread->hooks[0].context;
	fsched_info_t* old_queue = sched_private ? sched_private->queue : NULL;

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
	sched_private->queue = NULL;

	if (!queue_is_locked) {
		flock_spin_intsafe_unlock(&old_queue->lock);
	}
};

// returns with the queue in the same lock state as on entry
static void add_to_queue(fthread_t* thread, fsched_info_t* new_queue, bool new_queue_is_locked) {
	fthread_private_t* private_thread = (void*)thread;
	fsched_thread_private_t* sched_private = private_thread->hooks[0].context;
	fsched_info_t* old_queue = sched_private ? sched_private->queue : NULL;

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

	sched_private->queue = new_queue;

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

	fsched_per_cpu_info()->last_timer_id = FTIMERS_ID_INVALID;

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
	} else if (queue->count == 1 && old_thread == idle_thread) {
		fthread_t* new_thread = queue->head;

		flock_spin_intsafe_lock(&old_thread->lock);
		fthread_state_execution_write_locked(old_thread, fthread_state_execution_not_running);
		flock_spin_intsafe_unlock(&old_thread->lock);

		flock_spin_intsafe_lock(&new_thread->lock);
		fthread_state_execution_write_locked(new_thread, fthread_state_execution_interrupted);
		flock_spin_intsafe_unlock(&new_thread->lock);

		fsched_switch(old_thread, new_thread);
	} else {
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
	if (fsched_per_cpu_info()->last_timer_id != FTIMERS_ID_INVALID) {
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
	fsched_thread_private_t* sched_private = NULL;

	farch_sched_init();

	fsched_info_count = fcpu_count();

	if (fmempool_allocate(sizeof(fsched_info_t*) * fsched_info_count, NULL, (void*)&fsched_infos) != ferr_ok) {
		fpanic("Failed to allocate scheduler information pointer array");
	}

	if (fmempool_allocate(sizeof(fthread_t*) * fsched_info_count, NULL, (void*)&idle_threads) != ferr_ok) {
		fpanic("Failed to allocate idle thread pointer array");
	}

	for (size_t i = 0; i < fsched_info_count; ++i) {
		fthread_private_t* private_idle_thread;

		if (fmempool_allocate(sizeof(fsched_info_t), NULL, (void*)&fsched_infos[i]) != ferr_ok) {
			fpanic("Failed to allocate scheduler information structure for CPU %lu", i);
		}

		if (fthread_new(scheduler_idle, NULL, NULL, IDLE_THREAD_STACK_SIZE, 0, &idle_threads[i]) != ferr_ok) {
			fpanic("Failed to create idle thread for CPU %lu", i);
		}

		private_idle_thread = (void*)idle_threads[i];
		private_idle_thread->hooks[0].suspend = manager_suspend;
		private_idle_thread->hooks[0].resume = manager_resume;
		private_idle_thread->hooks[0].kill = manager_kill;
		private_idle_thread->hooks[0].interrupted = manager_interrupted;
		private_idle_thread->hooks[0].ending_interrupt = manager_ending_interrupt;
		private_idle_thread->hooks[0].owner_id = SCHEDULER_HOOK_OWNER_ID;
		private_idle_thread->hooks[0].context = NULL;
		private_idle_thread->hooks_in_use |= 1 << 0;

		flock_spin_intsafe_init(&fsched_infos[i]->lock);
		fsched_infos[i]->head = NULL;
		fsched_infos[i]->tail = NULL;
		fsched_infos[i]->count = 0;
		fsched_infos[i]->last_timer_id = FTIMERS_ID_INVALID;
		fsched_infos[i]->active = false; // on startup, other CPUs are still sleeping
	}

	this_info = fsched_per_cpu_info();

	this_info->active = true;

	if (fmempool_allocate(sizeof(fsched_thread_private_t), NULL, (void*)&sched_private) != ferr_ok) {
		fpanic("Failed to allocate private thread manager context for scheduler for bootstrap thread");
	}

	flock_spin_intsafe_lock(&thread->lock);

	thread->prev = thread;
	thread->next = thread;
	thread->id = get_next_id();
	sched_private->global_prev = NULL;
	sched_private->global_next = NULL;
	sched_private->queue = this_info;
	private_thread->hooks[0].suspend = manager_suspend;
	private_thread->hooks[0].resume = manager_resume;
	private_thread->hooks[0].kill = manager_kill;
	private_thread->hooks[0].interrupted = manager_interrupted;
	private_thread->hooks[0].ending_interrupt = manager_ending_interrupt;
	private_thread->hooks[0].owner_id = SCHEDULER_HOOK_OWNER_ID;
	private_thread->hooks[0].context = sched_private;
	private_thread->hooks_in_use |= 1 << 0;

	thread->state &= ~fthread_state_pending_suspend;
	fthread_state_execution_write_locked(thread, fthread_state_execution_running);

	this_info->head = thread;
	this_info->tail = thread;
	++this_info->count;

	global_thread_list_add(thread);

	flock_spin_intsafe_unlock(&thread->lock);

	// this will also arm the timer
	fsched_bootstrap(thread);
};

// must hold NO queue locks
// returns a scheduler information structure with its lock held
static fsched_info_t* find_lightest_load(void) {
	fsched_info_t* result = NULL;
	for (size_t i = 0; i < fsched_info_count; ++i) {
		size_t prev_count = result ? result->count : SIZE_MAX;
		if (result) {
			flock_spin_intsafe_unlock(&result->lock);
		}
		// yes, dropping the previous one's lock before acquire this one's lock means the count might've changed.
		// however, if we hold the lock, we can run into deadlocks; so let's prefer to be a little bit inaccurate rather than frozen.
		flock_spin_intsafe_lock(&fsched_infos[i]->lock);
		if (fsched_infos[i]->active && prev_count > fsched_infos[i]->count) {
			result = fsched_infos[i];
		} else {
			flock_spin_intsafe_unlock(&fsched_infos[i]->lock);
			flock_spin_intsafe_lock(&result->lock);
		}
	}
	return result;
};

static ferr_t manager_kill(void* context, fthread_t* thread) {
	fthread_private_t* private_thread = (void*)thread;
	fthread_state_execution_t prev_exec_state = fthread_state_execution_read_locked(thread);
	fsched_thread_private_t* sched_private = private_thread->hooks[0].context;
	fsched_info_t* old_queue = sched_private ? sched_private->queue : NULL;

	// at this point, the threads subsystem has already ensured that:
	//   * the thread is not already dead.
	//   * the pending death bit has been set.

	if (prev_exec_state != fthread_state_execution_running && prev_exec_state != fthread_state_execution_interrupted) {
		// if it's not running, that's wonderful! our job is much easier.
		// note that the thread currently being interrupted is an issue because we don't know if it's the one being switched out or the one being switched in.

		// if the thread was on a waitq's waiting list, remove it now
		if ((prev_exec_state == fthread_state_execution_suspended || (thread->state & fthread_state_pending_suspend) != 0) && thread->waitq) {
			if ((thread->state & fthread_state_holding_waitq_lock) == 0) {
				fwaitq_lock(thread->waitq);
			} else {
				// HACK: compensate for the fint_enable() performed by unlocking the waitq lock
				fint_disable();
			}
			fwaitq_remove_locked(thread->waitq, &thread->wait_link);
			thread->state &= ~fthread_state_holding_waitq_lock;
			fwaitq_unlock(thread->waitq);
		}
		thread->waitq = NULL;

		if (private_thread->timer_id != FTIMERS_ID_INVALID) {
			FERRO_WUR_IGNORE(ftimers_cancel(private_thread->timer_id));
		}
		private_thread->timer_id = FTIMERS_ID_INVALID;
		private_thread->pending_timeout_value = 0;

		fthread_state_execution_write_locked(thread, fthread_state_execution_dead);
		thread->state &= ~(fthread_state_pending_death | fthread_state_pending_suspend);
		remove_from_queue(thread, false);

		global_thread_list_remove(thread);

		fthread_died(thread);
		fthread_release(thread); // this releases the thread manager's reference on it

		return ferr_permanent_outage;
	}

	// otherwise, it's currently running, so we'll have to ask our arch-specific code to stop it immediately

	// we don't want to be interrupted by the timer if it's for our current thread
	fint_disable();

	// unlock it for the call
	flock_spin_intsafe_unlock(&thread->lock);

	if (thread == fthread_current()) {
		// if it's the current thread, we're not returning, so we need to release the extra reference that fthread_kill() acquired
		fthread_release(thread);
	}

	// this does not return if `thread == fthread_current()`
	fsched_preempt_thread(thread);

	// it might seem like the thread might be fully released here, but actually no.
	// fthread_kill() retains the thread before calling us and then releases it afterwards.

	// and relock it for the threads subsystem
	flock_spin_intsafe_lock(&thread->lock);

	fint_enable();

	// that's it; once the thread returns to the context switcher, it should see that it's dying and finish the job
	return ferr_permanent_outage;
};

static void timeout_callback(void* data) {
	fthread_t* thread = data;
	fthread_private_t* private_thread = data;

	flock_spin_intsafe_lock(&thread->lock);
	private_thread->timer_id = FTIMERS_ID_INVALID;
	flock_spin_intsafe_unlock(&thread->lock);

	FERRO_WUR_IGNORE(fthread_resume(thread));
};

static ferr_t manager_suspend(void* context, fthread_t* thread) {
	fthread_private_t* private_thread = (void*)thread;
	fthread_state_execution_t prev_exec_state = fthread_state_execution_read_locked(thread);
	fsched_thread_private_t* sched_private = private_thread->hooks[0].context;
	fsched_info_t* old_queue = sched_private ? sched_private->queue : NULL;

	// at this point, the threads subsystem has already ensured that:
	//   * the thread is neither dead nor dying.
	//   * the thread is neither fsched_suspended nor pending suspension.
	//   * the pending suspension bit has been set.

	// if it's not currently running, we can take care of it right now
	if (prev_exec_state != fthread_state_execution_running && prev_exec_state != fthread_state_execution_interrupted) {
		remove_from_queue(thread, false);

		// the suspension is no longer pending; it's now fully fsched_suspended
		fthread_state_execution_write_locked(thread, fthread_state_execution_suspended);
		thread->state &= ~fthread_state_pending_suspend;

		add_to_queue(thread, &fsched_suspended, false);

		// if we want to wait for a waitq, add ourselves to its waiting list now
		if (thread->waitq) {
			if ((thread->state & fthread_state_holding_waitq_lock) == 0) {
				fwaitq_lock(thread->waitq);
			} else {
				// HACK: compensate for the fint_enable() performed by unlocking the waitq lock
				fint_disable();
			}
			fwaitq_add_locked(thread->waitq, &thread->wait_link);
			thread->state &= ~fthread_state_holding_waitq_lock;
			fwaitq_unlock(thread->waitq);
		}

		// if we want a timeout, set it up now
		if (private_thread->pending_timeout_value > 0) {
			if (private_thread->pending_timeout_type == fthread_timeout_type_ns_relative) {
				if (ftimers_oneshot_blocking(private_thread->pending_timeout_value, timeout_callback, thread, &private_thread->timer_id) != ferr_ok) {
					fpanic("Failed to set up thread wakeup timeout");
				}
			} else {
				fpanic("Unsupported timeout type: %d", private_thread->pending_timeout_type);
			}
		}

		return ferr_permanent_outage;
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
	return ferr_permanent_outage;
};

static ferr_t manager_resume(void* context, fthread_t* thread) {
	fthread_private_t* private_thread = (void*)thread;
	fthread_state_execution_t prev_exec_state = fthread_state_execution_read_locked(thread);
	fsched_thread_private_t* sched_private = private_thread->hooks[0].context;
	fsched_info_t* old_queue = sched_private ? sched_private->queue : NULL;
	fsched_info_t* new_queue = NULL;

	// at this point, the threads subsystem has already ensured that:
	//   * the thread is neither dead nor dying.
	//   * the thread is either fsched_suspended or pending suspension.
	//   * if it was pending suspension, the pending suspension bit has been cleared.

	if (prev_exec_state != fthread_state_execution_suspended) {
		// if it's not currently fsched_suspended, it's already scheduled on a CPU.
		// in that case, clearing the pending suspension is enough to keep it running,
		// which the threads subsystem already does for us.

		// we haven't been fsched_suspended yet, so the thread isn't on the waitq's waiting list yet
		thread->waitq = NULL;

		return ferr_permanent_outage;
	}

	// if the thread was on a waitq's waiting list, remove it now
	if (thread->waitq) {
		if ((thread->state & fthread_state_holding_waitq_lock) == 0) {
			fwaitq_lock(thread->waitq);
		} else {
			// HACK: compensate for the fint_enable() performed by unlocking the waitq lock
			fint_disable();
		}
		fwaitq_remove_locked(thread->waitq, &thread->wait_link);
		thread->state &= ~fthread_state_holding_waitq_lock;
		fwaitq_unlock(thread->waitq);
	}
	thread->waitq = NULL;

	// if it's got a timeout, cancel it now
	if (private_thread->timer_id != FTIMERS_ID_INVALID) {
		FERRO_WUR_IGNORE(ftimers_cancel(private_thread->timer_id));
	}
	private_thread->timer_id = FTIMERS_ID_INVALID;
	private_thread->pending_timeout_value = 0;

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

	// unlock the queue that was locked by find_lightest_load()
	flock_spin_intsafe_unlock(&new_queue->lock);

	return ferr_permanent_outage;
};

static fthread_t* clear_pending_death_or_suspension(fthread_t* thread) {
	fthread_private_t* private_thread = (void*)thread;
	fsched_info_t* queue = fsched_per_cpu_info();

	flock_spin_intsafe_lock(&queue->lock);

	// we should only have at most a single thread waiting for death or suspension, and it should only be the active thread.
	// all other threads aren't running, so when they're asked to be killed or fsched_suspended, they can do it immediately.
	if ((thread->state & (fthread_state_pending_death | fthread_state_pending_suspend)) != 0) {
		bool needs_to_suspend = false;
		fthread_t* new_thread = thread->next;
		fthread_state_execution_t prev_exec_state = fthread_state_execution_read_locked(thread);
		fsched_thread_private_t* sched_private = private_thread->hooks[0].context;
		fsched_info_t* old_queue = sched_private ? sched_private->queue : NULL;

		if (old_queue != queue) {
			fpanic("Thread information inconsistency (dying thread's queue is not current CPU's queue)");
		}

		if (new_thread == thread) {
			// that means we've reached the end of this queue; the new thread will instead be the idle thread for this CPU
			new_thread = idle_threads[fcpu_id()];
		}

		// save the thread's context and load the context for the new thread
		fsched_switch(thread, new_thread);

		// mark it as dead or fsched_suspended (depending on what we want)
		if ((thread->state & fthread_state_pending_death) != 0) {
			fthread_state_execution_write_locked(thread, fthread_state_execution_dead);
		} else {
			needs_to_suspend = true;
			fthread_state_execution_write_locked(thread, fthread_state_execution_suspended);
		}

		// clear the pending status(es) and remove it from the queue
		thread->state &= ~(fthread_state_pending_death | fthread_state_pending_suspend);
		remove_from_queue(thread, true);

		// if it needs to be fsched_suspended, it needs to be added to the suspension queue
		if (needs_to_suspend) {
			add_to_queue(thread, &fsched_suspended, false);

			// if we want to wait for a waitq, add ourselves to its waiting list now
			if (thread->waitq) {
				if ((thread->state & fthread_state_holding_waitq_lock) == 0) {
					fwaitq_lock(thread->waitq);
				} else {
					// HACK: compensate for the fint_enable() performed by unlocking the waitq lock
					fint_disable();
				}
				fwaitq_add_locked(thread->waitq, &thread->wait_link);
				thread->state &= ~fthread_state_holding_waitq_lock;
				fwaitq_unlock(thread->waitq);
			}

			// if we want a timeout, set it up now
			if (private_thread->pending_timeout_value > 0) {
				if (private_thread->pending_timeout_type == fthread_timeout_type_ns_relative) {
					if (ftimers_oneshot_blocking(private_thread->pending_timeout_value, timeout_callback, thread, &private_thread->timer_id) != ferr_ok) {
						fpanic("Failed to set up thread wakeup timeout");
					}
				} else {
					fpanic("Unsupported timeout type: %d", private_thread->pending_timeout_type);
				}
			}

			flock_spin_intsafe_unlock(&thread->lock);

			// drop the queue lock here (because we also drop it in the alternative branch)
			flock_spin_intsafe_unlock(&queue->lock);
		} else {
			// if the thread was on a waitq's waiting list, remove it now
			if (prev_exec_state == fthread_state_execution_suspended && thread->waitq) {
				if ((thread->state & fthread_state_holding_waitq_lock) == 0) {
					fwaitq_lock(thread->waitq);
				} else {
					// HACK: compensate for the fint_enable() performed by unlocking the waitq lock
					fint_disable();
				}
				fwaitq_remove_locked(thread->waitq, &thread->wait_link);
				thread->state &= ~fthread_state_holding_waitq_lock;
				fwaitq_unlock(thread->waitq);
			}
			thread->waitq = NULL;

			if (private_thread->timer_id != FTIMERS_ID_INVALID) {
				FERRO_WUR_IGNORE(ftimers_cancel(private_thread->timer_id));
			}
			private_thread->timer_id = FTIMERS_ID_INVALID;
			private_thread->pending_timeout_value = 0;

			global_thread_list_remove(thread);

			// drop the lock now; everyone else will see the thread is dead and not use it for further execution
			flock_spin_intsafe_unlock(&thread->lock);

			// unlock the queue in case the following calls need to use it
			flock_spin_intsafe_unlock(&queue->lock);

			fthread_died(thread);
			fthread_release(thread);
		}

		// XXX: not sure about this next part, but it *seems* fine

		// the active thread may have changed while we had the lock dropped, so check again
		flock_spin_intsafe_lock(&queue->lock);

		thread = queue->head ? queue->head : idle_threads[fcpu_id()];
		private_thread = (void*)thread;
		// the queue should still be the same

		if (thread != new_thread) {
			fsched_switch(NULL, thread);
		}

		flock_spin_intsafe_lock(&thread->lock);
	}

	flock_spin_intsafe_unlock(&queue->lock);

	return thread;
};

static ferr_t manager_interrupted(void* context, fthread_t* thread) {
	flock_spin_intsafe_lock(&thread->lock);
	thread = clear_pending_death_or_suspension(thread);
	fthread_state_execution_write_locked(thread, fthread_state_execution_interrupted);
	flock_spin_intsafe_unlock(&thread->lock);
	return ferr_ok;
};

static ferr_t manager_ending_interrupt(void* context, fthread_t* thread) {
	// we actually can't have any more threads to clear due to death or suspension right here
	// because kill and suspend immediately remove the threads if they're not running
	// (and being interrupted counts as not running)
	//thread = clear_pending_death_or_suspension(thread);

	flock_spin_intsafe_lock(&thread->lock);
	fthread_state_execution_write_locked(thread, fthread_state_execution_running);
	flock_spin_intsafe_unlock(&thread->lock);

	return ferr_ok;
};

ferr_t fsched_manage(fthread_t* thread) {
	fthread_private_t* private_thread = (void*)thread;
	fthread_state_execution_t prev_exec_state;
	ferr_t status = ferr_ok;
	fsched_thread_private_t* sched_private = NULL;

	if (!thread) {
		status = ferr_invalid_argument;
		goto out_unlocked;
	}

	if (fthread_retain(thread) != ferr_ok) {
		// it was fully released before we managed to retain it
		status = ferr_invalid_argument;
		goto out_unlocked;
	}

	if (fmempool_allocate(sizeof(fsched_thread_private_t), NULL, (void*)&sched_private) != ferr_ok) {
		fthread_release(thread);
		status = ferr_invalid_argument;
		goto out_unlocked;
	}

	sched_private->global_next = NULL;
	sched_private->global_prev = NULL;
	sched_private->queue = NULL;

	flock_spin_intsafe_lock(&thread->lock);

	private_thread->hooks[0].context = sched_private;

	prev_exec_state = fthread_state_execution_read_locked(thread);

	if (prev_exec_state == fthread_state_execution_dead || (thread->state & fthread_state_pending_death) != 0) {
		// well, we can't manage a dead thread
		status = ferr_invalid_argument;
		goto out;
	}

	// we now have to set everything on the thread needed to mark it as fsched_suspended

	thread->prev = NULL;
	thread->next = NULL;
	private_thread->hooks[0].suspend = manager_suspend;
	private_thread->hooks[0].resume = manager_resume;
	private_thread->hooks[0].kill = manager_kill;
	private_thread->hooks[0].interrupted = manager_interrupted;
	private_thread->hooks[0].ending_interrupt = manager_ending_interrupt;
	private_thread->hooks[0].owner_id = SCHEDULER_HOOK_OWNER_ID;
	private_thread->hooks_in_use |= 1 << 0;
	thread->id = get_next_id();

	fthread_state_execution_write_locked(thread, fthread_state_execution_suspended);
	thread->state &= ~fthread_state_pending_suspend;

	add_to_queue(thread, &fsched_suspended, false);

	global_thread_list_add(thread);

out:
	flock_spin_intsafe_unlock(&thread->lock);

	if (status != ferr_ok) {
		fthread_release(thread);
	}

out_unlocked:
	return status;
};

void fsched_foreach_thread(fsched_thread_iterator_f iterator, void* data, bool include_suspended) {
	flock_spin_intsafe_lock(&global_thread_lock);

	for (fthread_t* thread = global_thread_list; thread != NULL; thread = ((fsched_thread_private_t*)((fthread_private_t*)thread)->hooks[0].context)->global_next) {
		// this is racy because we don't have the thread lock held, but we also don't want to lock up if someone is holding the thread's lock and wants the global thread list lock
		if (!include_suspended && fthread_execution_state(thread) == fthread_state_execution_suspended) {
			continue;
		}

		if (!iterator(data, thread)) {
			break;
		}
	}

	flock_spin_intsafe_unlock(&global_thread_lock);
};

fthread_t* fsched_find(fthread_id_t thread_id, bool retain) {
	flock_spin_intsafe_lock(&global_thread_lock);

	for (fthread_t* thread = global_thread_list; thread != NULL; thread = ((fsched_thread_private_t*)((fthread_private_t*)thread)->hooks[0].context)->global_next) {
		if (thread->id == thread_id) {
			if (retain) {
				// we know that the thread can't be dead here because we own a reference to it and that can't go away as long as we have the global thread list lock
				FERRO_WUR_IGNORE(fthread_retain(thread));
			}
			flock_spin_intsafe_unlock(&global_thread_lock);
			return thread;
		}
	}

	flock_spin_intsafe_unlock(&global_thread_lock);

	return NULL;
};
