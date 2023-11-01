/*
 * This file is part of Anillo OS
 * Copyright (C) 2022 Anillo OS Developers
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

#include <ferro/core/cpu.private.h>
#include <ferro/core/mempool.h>
#include <ferro/core/interrupts.h>
#include <ferro/core/timers.h>

fcpu_interrupt_work_queue_t fcpu_broadcast_queue = {
	.lock = FLOCK_SPIN_INTSAFE_INIT,
	.head = NULL,
	.tail = NULL,
};

static fcpu_interrupt_work_id_t next_work_id = fcpu_interrupt_work_id_invalid + 1;

fcpu_interrupt_work_id_t fcpu_interrupt_work_next_id(void) {
	fcpu_interrupt_work_id_t work_id = fcpu_interrupt_work_id_invalid;

retry:
	work_id = __atomic_fetch_add(&next_work_id, 1, __ATOMIC_RELAXED);

	if (work_id == fcpu_interrupt_work_id_invalid) {
		goto retry;
	}

	return work_id;
};

fcpu_interrupt_work_item_t* fcpu_interrupt_work_queue_next(fcpu_interrupt_work_queue_t* work_queue, fcpu_interrupt_work_id_t last_id) {
	fcpu_interrupt_work_item_t* work_item;

	flock_spin_intsafe_lock(&work_queue->lock);

	for (work_item = work_queue->head; work_item != NULL; work_item = work_item->next) {
		if ((work_item->flags & fcpu_interrupt_work_item_flag_exclude_origin) != 0 && fcpu_current_id() == work_item->origin) {
			// don't run this item on the originating CPU
			continue;
		}

		if (work_item->work_id <= last_id) {
			// we've already run this work item on this CPU
			continue;
		}

		++work_item->checkin_count;

		break;
	}

	flock_spin_intsafe_unlock(&work_queue->lock);

	return work_item;
};

void fcpu_interrupt_work_queue_add(fcpu_interrupt_work_queue_t* work_queue, fcpu_interrupt_work_item_t* work_item) {
	flock_spin_intsafe_lock(&work_queue->lock);

	work_item->prev = work_queue->tail ? &work_queue->tail->next : &work_queue->head;
	work_item->next = *work_item->prev;

	*work_item->prev = work_item;
	if (work_item->next) {
		work_item->next->prev = &work_item->next;
	}
	work_queue->tail = work_item;

	work_item->queue = work_queue;

	flock_spin_intsafe_unlock(&work_queue->lock);
};

void fcpu_interrupt_work_item_checkout(fcpu_interrupt_work_item_t* work_item) {
	fcpu_interrupt_work_queue_t* work_queue = work_item->queue;
	uint64_t expected_count = work_item->expected_count;

	// needs to be releasing so that it synchronizes with the acquiring load later on in the loop.
	// this ensures that we load the variables above before the item is destroyed/invalidated.
	uint64_t checkout_count = __atomic_add_fetch(&work_item->checkout_count, 1, __ATOMIC_RELEASE);

	// if this checkout is less than the expected count, we *definitely* can't be completely finished with this work item yet.
	if (checkout_count < expected_count) {
		return;
	}

	// we've reached the expected checkout count.
	// let's go ahead and unqueue it and mark it as completed or free it.

	flock_spin_intsafe_lock(&work_queue->lock);

	// clear out items that are fully checked out
	fcpu_interrupt_work_item_t* next = NULL;
	for (fcpu_interrupt_work_item_t* head_work_item = work_queue->head; head_work_item != NULL; head_work_item = next) {
		next = head_work_item->next;

		// if this checkout is less than the checkin count, someone else is still running the work, so don't touch the work item
		//
		// note that this is separate from the expected count; the expected count simply tells us how many CPUs need to do the work
		// before it can be considered completed. the checkin count tells us how many CPUs *actually* started doing the work.
		// the checkout count tells us how many CPUs have completed the work. it's possible for the checkin count to be greater
		// than the expected count (e.g. if a CPU comes online after the work was enqueued but before it was fully completed).
		//
		// synchronizes with the releasing add+fetch at the beginning of this function
		if (__atomic_load_n(&head_work_item->checkout_count, __ATOMIC_ACQUIRE) < head_work_item->checkin_count) {
			break;
		}

		if (!head_work_item->next) {
			work_queue->tail = NULL;
		}

		*head_work_item->prev = head_work_item->next;
		if (head_work_item->next) {
			head_work_item->next->prev = head_work_item->prev;
		}

		head_work_item->queue = NULL;
		head_work_item->prev = NULL;
		head_work_item->next = NULL;

		if (head_work_item->flags & fcpu_interrupt_work_item_flag_free_on_finish) {
			// free the work item
			FERRO_WUR_IGNORE(fmempool_free(head_work_item));
		} else {
			// mark it as completed
			__atomic_fetch_or(&head_work_item->flags, fcpu_interrupt_work_item_flag_completed, __ATOMIC_RELEASE);
		}
	}

	flock_spin_intsafe_unlock(&work_queue->lock);
};

// broadcast another IPI if we're waiting and the work hasn't completed within this amount of time
// (currently 5ms)
#define IPI_DO_TIMEOUT 0
#define IPI_TIMEOUT 5ull * 1000 * 1000

ferr_t fcpu_interrupt_all(fcpu_interrupt_work_f work, void* context, bool include_current, bool wait) {
	ferr_t status = ferr_ok;
	fcpu_interrupt_work_item_t stack_work_item;
	fcpu_interrupt_work_item_t* work_item;

	if (!include_current && fcpu_online_count() == 0) {
		// we're the only running CPU
		return ferr_ok;
	}

	if (wait) {
		work_item = &stack_work_item;
	} else {
		status = fmempool_allocate_advanced(sizeof(*work_item), 0, UINT8_MAX, fmempool_flag_prebound, NULL, (void*)&work_item);
		if (status != ferr_ok) {
			goto out;
		}
	}

	work_item->prev = NULL;
	work_item->next = NULL;
	work_item->queue = NULL;
	work_item->flags = include_current ? 0 : fcpu_interrupt_work_item_flag_exclude_origin;
	work_item->origin = fcpu_current_id();
	work_item->work = work;
	work_item->context = context;
	work_item->expected_count = fcpu_online_count();
	work_item->checkin_count = include_current ? 0 : 1;
	work_item->checkout_count = include_current ? 0 : 1;
	work_item->work_id = fcpu_interrupt_work_next_id();

	if (!wait) {
		work_item->flags |= fcpu_interrupt_work_item_flag_free_on_finish;
	}

	fcpu_interrupt_work_queue_add(&fcpu_broadcast_queue, work_item);

	status = fcpu_arch_interrupt_all(include_current);
	if (status != ferr_ok) {
		if (!wait) {
			FERRO_WUR_IGNORE(fmempool_free(work_item));
		}
	}

	if (wait) {
		// we should do IPI work in the loop if we have interrupts disabled
		bool should_do_work = fint_save() > 0;

#if IPI_DO_TIMEOUT
		ftimers_timestamp_t start_ts;
		ftimers_timestamp_t end_ts;
		uint64_t delta_ns;

		fpanic_status(ftimers_timestamp_read(&start_ts));
#endif

		while ((__atomic_load_n(&work_item->flags, __ATOMIC_RELAXED) & fcpu_interrupt_work_item_flag_completed) == 0) {
#if IPI_DO_TIMEOUT
			fpanic_status(ftimers_timestamp_read(&end_ts));
			fpanic_status(ftimers_timestamp_delta_to_ns(start_ts, end_ts, &delta_ns));

			if (delta_ns >= IPI_TIMEOUT) {
				FERRO_WUR_IGNORE(fcpu_arch_interrupt_all(include_current));
			}
#endif

			if (should_do_work) {
				fcpu_do_work();
			}

			farch_lock_spin_yield();
		}

		// synchronize with other CPUs
		__atomic_thread_fence(__ATOMIC_ACQUIRE);
	}

out:
	return status;
};
