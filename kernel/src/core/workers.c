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
 * Implementation of threaded workers.
 *
 * Workers are useful to schedule some work to run later on a thread
 * without having to worry about managing the thread.
 *
 * These are very useful for interrupts to quickly store some information and then schedule a worker to process it later.
 */

#include <ferro/core/workers.h>
#include <ferro/core/locks.h>
#include <ferro/core/mempool.h>
#include <ferro/core/scheduler.h>
#include <ferro/core/threads.h>
#include <ferro/core/panic.h>
#include <ferro/core/cpu.h>
#include <ferro/core/waitq.private.h>
#include <ferro/core/interrupts.h>
#include <ferro/core/entry.h>
#include <ferro/core/threads.private.h>

#include <ferro/core/console.h>
#include <ferro/core/paging.h>
#include <ferro/core/refcount.h>

#include <stdatomic.h>

FERRO_ENUM(uint8_t, fwork_state) {
	fwork_state_pending,
	fwork_state_cancelled,
	fwork_state_running,
	fwork_state_complete,
};

FERRO_STRUCT_FWD(fwork_queue);

FERRO_STRUCT(fwork) {
	fwork_t* prev;
	fwork_t* next;
	fwork_queue_t* queue;
	frefcount_t reference_count;
	fworker_f function;
	void* data;
	fwork_flags_t flags;

	/**
	 * Waitq that can be used to wait for the work to complete.
	 *
	 * The waitq's lock is also used to protect #state.
	 */
	fwaitq_t waitq;

	/**
	 * The current state of the work. See ::fwork_state for more details.
	 */
	fwork_state_t state;

	ftimers_id_t timer_id;

	size_t reschedule_count;
};

FERRO_STRUCT(fwork_queue) {
	flock_spin_intsafe_t lock;
	fwork_t* head;
	fwork_t* tail;

	/**
	 * The size of the work load.
	 *
	 * @note This number is not necessarily equal to the the number of ::fwork nodes currently in the queue.
	 *       Because it represents the work load, it is also incremented (through the reservation system) when items are waiting to be added to the queue.
	 */
	size_t length;

	/**
	 * The worker thread used to process the queue.
	 */
	fthread_t* thread;

	/**
	 * Used by the worker thread to sleep until more workers are added.
	 */
	flock_semaphore_t semaphore;
};

static fwork_queue_t** worker_queues = NULL;
static size_t worker_queue_count = 0;

static void worker_thread_runner(void* data);

static void fwork_queue_lock(fwork_queue_t* queue) {
	flock_spin_intsafe_lock(&queue->lock);
};

static void fwork_queue_unlock(fwork_queue_t* queue) {
	flock_spin_intsafe_unlock(&queue->lock);
};

// the queue's lock MUST be held
static void fwork_queue_push_locked(fwork_queue_t* queue, fwork_t* work) {
	work->prev = queue->tail;
	work->next = NULL;
	work->queue = queue;

	if (work->prev) {
		work->prev->next = work;
	}

	if (!queue->head) {
		queue->head = work;
	}
	queue->tail = work;

	++queue->length;

	flock_semaphore_up(&queue->semaphore);
};

// the queue's lock MUST be held
static void fwork_queue_remove_locked(fwork_queue_t* queue, fwork_t* work) {
	if (work->prev) {
		work->prev->next = work->next;
	} else {
		queue->head = work->next;
	}

	if (work->next) {
		work->next->prev = work->prev;
	} else {
		queue->tail = work->prev;
	}

	work->prev = NULL;
	work->next = NULL;
	work->queue = NULL;

	--queue->length;
};

// the queue's lock must NOT be held
static void fwork_queue_remove(fwork_queue_t* queue, fwork_t* work) {
	fwork_queue_lock(queue);
	fwork_queue_remove_locked(queue, work);
	fwork_queue_unlock(queue);
};

// the queue's lock must NOT be held
static fwork_t* fwork_queue_pop(fwork_queue_t* queue) {
	fwork_t* result = NULL;
	fwork_queue_lock(queue);
	if (queue->head) {
		result = queue->head;
		fwork_queue_remove_locked(queue, result);
	}
	fwork_queue_unlock(queue);
	return result;
};

/**
 * Reserves space in the given queue for the given work instance, but does not actually add it.
 *
 * This is useful because the queue length is used to determine which work queue has the lightest load.
 *
 * It also associates the queue with the given work instance, in case the work is cancelled before it is fully added.
 *
 * @pre The queue's lock MUST be held.
 */
static void fwork_queue_reserve_locked(fwork_queue_t* queue, fwork_t* work) {
	work->prev = NULL;
	work->next = NULL;
	work->queue = queue;

	++queue->length;
};

/**
 * Adds the given work instance to the given work queue, assuming that it had already been previously reserved.
 *
 * @pre The queue's lock MUST be held.
 */
static void fwork_queue_complete_reservation_locked(fwork_queue_t* queue, fwork_t* work) {
	work->prev = queue->tail;
	work->next = NULL;
	work->queue = queue;

	if (work->prev) {
		work->prev->next = work;
	}

	if (!queue->head) {
		queue->head = work;
	}
	queue->tail = work;

	flock_semaphore_up(&queue->semaphore);
};

/**
 * Undoes the work of fwork_queue_reserve_locked(), assuming it had not been completed yet.
 *
 * @pre The queue's lock MUST be held.
 */
static void fwork_queue_cancel_reservation_locked(fwork_queue_t* queue, fwork_t* work) {
	work->prev = NULL;
	work->next = NULL;
	work->queue = NULL;

	--queue->length;
};

ferr_t fwork_new(fworker_f worker_function, void* data, fwork_flags_t flags, fwork_t** out_worker) {
	fwork_t* work = NULL;

	if (!worker_function || !out_worker) {
		return ferr_invalid_argument;
	}

	// needs to be prebound because page fault handlers need to schedule workers in some cases
	if (fmempool_allocate_advanced(sizeof(fwork_t), 0, UINT8_MAX, fmempool_flag_prebound, NULL, (void**)&work) != ferr_ok) {
		return ferr_temporary_outage;
	}

	work->prev = NULL;
	work->next = NULL;
	frefcount_init(&work->reference_count);
	work->function = worker_function;
	work->data = data;
	work->state = fwork_state_complete;
	work->timer_id = FTIMERS_ID_INVALID;
	fwaitq_init(&work->waitq);
	work->flags = flags;
	work->reschedule_count = 0;

	*out_worker = work;

	return ferr_ok;
};

static void fwork_destroy(fwork_t* work) {
	if (fmempool_free(work) != ferr_ok) {
		fpanic("Failed to free work instance structure");
	}
};

ferr_t fwork_retain(fwork_t* work) {
	return frefcount_increment(&work->reference_count);
};

void fwork_release(fwork_t* work) {
	if (frefcount_decrement(&work->reference_count) != ferr_permanent_outage) {
		return;
	}

	fwork_destroy(work);
};

static fwork_queue_t* fwork_queue_new(void) {
	fwork_queue_t* queue = NULL;

	if (fmempool_allocate_advanced(sizeof(fwork_queue_t), 0, UINT8_MAX, fmempool_flag_prebound, NULL, (void**)&queue) != ferr_ok) {
		return NULL;
	}

	queue->head = NULL;
	queue->tail = NULL;
	queue->length = 0;
	flock_spin_intsafe_init(&queue->lock);
	flock_semaphore_init(&queue->semaphore, 0);

#if 0
	fconsole_logf("work queue head phys: %p\n", (void*)fpage_virtual_to_physical((uintptr_t)&queue->head));
#endif

	if (fthread_new(worker_thread_runner, queue, NULL, 2ULL * 1024 * 1024, 0, &queue->thread) != ferr_ok) {
		FERRO_WUR_IGNORE(fmempool_free(queue));
		return NULL;
	}

	if (fsched_manage(queue->thread) != ferr_ok) {
		fthread_release(queue->thread);
		FERRO_WUR_IGNORE(fmempool_free(queue));
		return NULL;
	}

	if (fthread_resume(queue->thread) != ferr_ok) {
		FERRO_WUR_IGNORE(fthread_kill(queue->thread));
		fthread_release(queue->thread);
		FERRO_WUR_IGNORE(fmempool_free(queue));
		return NULL;
	}

	return queue;
};

void fworkers_init(void) {
#if 0
	worker_queue_count = fcpu_count();
#else
	worker_queue_count = 1;
#endif

	if (fmempool_allocate_advanced(sizeof(fthread_t*) * worker_queue_count, 0, UINT8_MAX, fmempool_flag_prebound, NULL, (void*)&worker_queues) != ferr_ok) {
		fpanic("Failed to allocate work queue pointer array");
	}

	for (size_t i = 0; i < worker_queue_count; ++i) {
		worker_queues[i] = fwork_queue_new();
		if (!worker_queues[i]) {
			fpanic("Failed to create a new work queue");
		}
	}
};

// very similar to the scheduler's find_lightest_load()
// returns with the queue lock held
static fwork_queue_t* find_lightest_load(void) {
	fwork_queue_t* result = NULL;
	for (size_t i = 0; i < worker_queue_count; ++i) {
		size_t prev_count = result ? result->length : SIZE_MAX;
		if (result) {
			fwork_queue_unlock(result);
		}
		// yes, dropping the previous one's lock before acquire this one's lock means the count might've changed.
		// however, if we hold the lock, we can run into deadlocks; so let's prefer to be a little bit inaccurate rather than frozen.
		fwork_queue_lock(worker_queues[i]);
		if (prev_count > worker_queues[i]->length) {
			result = worker_queues[i];
		} else {
			fwork_queue_unlock(worker_queues[i]);
			fwork_queue_lock(result);
		}
	}
	return result;
};

static void fwork_delayed_schedule(void* data) {
	fwork_t* work = data;

	fwaitq_lock(&work->waitq);
	if (work->state != fwork_state_pending) {
		// huh, weird. this shouldn't happen because cancelling the work should prevent us from ever being called.
		// oh well. just ignore it. fwork_cancel() will take care of releasing it.
		fwaitq_unlock(&work->waitq);
		return;
	}
	fwaitq_unlock(&work->waitq);

	work->timer_id = FTIMERS_ID_INVALID;

	// complete the reservation
	fwork_queue_lock(work->queue);
	fwork_queue_complete_reservation_locked(work->queue, work);
	fwork_queue_unlock(work->queue);
};

ferr_t fwork_schedule(fwork_t* work, uint64_t delay) {
	fwork_queue_t* queue;

	if (!work) {
		return ferr_invalid_argument;
	}

	fwaitq_lock(&work->waitq);

	if ((work->state == fwork_state_running || work->state == fwork_state_pending) && (work->flags & fwork_flag_allow_reschedule) != 0) {
		if (work->reschedule_count == 0 || (work->flags & (fwork_flag_repeated_reschedule | fwork_flag_balanced_reschedule)) != 0) {
			++work->reschedule_count;
		}
		fwaitq_unlock(&work->waitq);
		return ferr_ok;
	}

	if (work->state != fwork_state_complete && work->state != fwork_state_cancelled) {
		fwaitq_unlock(&work->waitq);
		return ferr_invalid_argument;
	}

	work->state = fwork_state_pending;

	fwaitq_unlock(&work->waitq);

	if (fwork_retain(work) != ferr_ok) {
		return ferr_permanent_outage;
	}

	queue = find_lightest_load();
	if (!queue) {
		fpanic("Failed to find work queue with lightest load (this is impossible)");
	}

	if (delay == 0) {
		fwork_queue_push_locked(queue, work);
	} else {
		fwork_queue_reserve_locked(queue, work);
		if (ftimers_oneshot_blocking(delay, fwork_delayed_schedule, work, &work->timer_id) != ferr_ok) {
			fwork_release(work);
			fwork_queue_cancel_reservation_locked(queue, work);
			fwork_queue_unlock(queue);
			return ferr_temporary_outage;
		}
	}

	fwork_queue_unlock(queue);

	return ferr_ok;
};

ferr_t fwork_schedule_new(fworker_f worker_function, void* data, uint64_t delay, fwork_t** out_work) {
	fwork_t* work = NULL;
	ferr_t status = ferr_ok;

	status = fwork_new(worker_function, data, 0, &work);
	if (status != ferr_ok) {
		return status;
	}

	status = fwork_schedule(work, delay);
	if (status != ferr_ok) {
		fwork_release(work);
		if (status == ferr_permanent_outage || status == ferr_invalid_argument) {
			fpanic("Impossible error returned from fwork_schedule()");
		}
		return status;
	}

	// fwork_schedule() retains the work, so if the user wants a reference, just give them ours.
	// otherwise, release our reference so that the one held by the queue is the only one on the work instance.
	if (out_work) {
		*out_work = work;
	} else {
		fwork_release(work);
	}

	return status;
};

ferr_t fwork_cancel(fwork_t* work) {
	if (!work) {
		return ferr_invalid_argument;
	}

	fwaitq_lock(&work->waitq);

	if (work->state != fwork_state_pending) {
		bool reschedule_cancelled = work->reschedule_count > 0;
		if (work->reschedule_count > 0) {
			--work->reschedule_count;
		}
		fwaitq_unlock(&work->waitq);
		return reschedule_cancelled ? ferr_ok : ferr_already_in_progress;
	}

	work->state = fwork_state_cancelled;

	fwaitq_unlock(&work->waitq);

	if (work->timer_id == FTIMERS_ID_INVALID) {
		fwork_queue_remove(work->queue, work);
	} else {
		fwork_queue_t* queue = work->queue;

		// cancel the timer
		FERRO_WUR_IGNORE(ftimers_cancel(work->timer_id));
		work->timer_id = FTIMERS_ID_INVALID;

		// and cancel the reservation
		fwork_queue_lock(queue);
		fwork_queue_cancel_reservation_locked(queue, work);
		fwork_queue_unlock(queue);
	}

	fwork_release(work);

	return ferr_ok;
};

static void fwork_interrupt_wakeup(void* data) {
	bool* keep_looping = data;
	*keep_looping = false;
};

// needs the waitq lock to be held; returns with it dropped
static void fwork_wait_raw(fwork_t* work) {
	// TODO: warn if we're going to block while in an interrupt context.
	//       that should definitely not be happening.
	if (fint_is_interrupt_context() || !fthread_current()) {
		fwaitq_waiter_t waiter;
		bool keep_looping = true;

		fwaitq_waiter_init(&waiter, fwork_interrupt_wakeup, &keep_looping);

		fwaitq_add_locked(&work->waitq, &waiter);
		fwaitq_unlock(&work->waitq);

		while (keep_looping) {
			fentry_idle();
		}
	} else {
		// fthread_wait_locked() will drop the waitq lock later
		FERRO_WUR_IGNORE(fthread_wait_locked(fthread_current(), &work->waitq));
	}
};

ferr_t fwork_wait(fwork_t* work) {
	ferr_t status = ferr_ok;

	// loop to properly handle spurious wakeups
	while (true) {
		fwaitq_lock(&work->waitq);

		if (work->state != fwork_state_pending && work->state != fwork_state_running) {
			// great; it's not pending and it's not running, so it must have been cancelled or completed
			status = (work->state == fwork_state_cancelled) ? ferr_cancelled : ferr_ok;
			fwaitq_unlock(&work->waitq);
			return status;
		}

		fwork_wait_raw(work);
	}
};

static void worker_thread_runner(void* data) {
	fwork_queue_t* queue = data;

	while (true) {
		fwork_t* work;

		// wait until we have something to work with
		flock_semaphore_down(&queue->semaphore);

		work = fwork_queue_pop(queue);

		// no work? there's a problem.
		if (!work) {
			fpanic("No work!");
			continue;
		}

		fwaitq_lock(&work->waitq);

		// if it's not pending, it's:
		//   * cancelled, so we shouldn't do anything with it
		//   * running? which would be weird, because that means someone else ran it.
		//   * complete? which would also be weird, because it would also mean someone else ran it.
		if (work->state != fwork_state_pending) {
			// in any case, if we can't run it, release it and try again for another work instance
			fwaitq_unlock(&work->waitq);
			fwork_release(work);
			continue;
		}

		// okay, we're about to start running it ourselves, so mark it as such
		work->state = fwork_state_running;
		fwaitq_unlock(&work->waitq);

		// now let's run it
		work->function(work->data);

		// okay, we're done running it, so mark it as such

		// first lock the queue
		fwaitq_lock(&work->waitq);

		// mark it as complete
		// anyone doing fwork_wait() should now see this and not add themselves to the waitq
		work->state = fwork_state_complete;

		// wake everyone up
		fwaitq_wake_many_locked(&work->waitq, SIZE_MAX);

		if (work->reschedule_count > 0) {
			fpanic("TODO: implement work rescheduling");
		}

		fwaitq_unlock(&work->waitq);

		// okay, we don't need the work instance anymore so we can release it
		fwork_release(work);

		// great, now we'll loop around again and try to process another work instance
	}
};
