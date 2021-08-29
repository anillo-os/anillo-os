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
//
// src/core/timers.c
//
// generic timer interface and management
// (actual firing is done by various backends)
//

#include <ferro/core/timers.private.h>
#include <ferro/core/locks.h>
#include <ferro/core/mempool.h>
#include <ferro/core/panic.h>
#include <ferro/core/console.h>

#include <stddef.h>

// this value is supposed to give the CPU a chance to something else other than just constantly firing timers
// (this value is in nanoseconds)
#define MIN_SCHED_DELAY_NS 1000ULL

FERRO_STRUCT(ftimers_timer) {
	ftimers_backend_timestamp_t most_recent_timestamp;
	uint64_t remaining_delay;
	ftimers_id_t id;
	ftimers_callback_f callback;
	void* data;
	bool disabled;
};

FERRO_STRUCT(ftimers_priority_queue) {
	ftimers_timer_t* timers;
	size_t length;
	size_t size;
};

#define MAX_BACKENDS 10

static const ftimers_backend_t* backends[MAX_BACKENDS] = {0};
static uint8_t backend_count = 0;
static uint8_t backend = UINT8_MAX;
// TODO: this should probably be an RW lock
static flock_spin_intsafe_t backend_lock = FLOCK_SPIN_INTSAFE_INIT;

static ftimers_priority_queue_t queue = {0};
#if 0
static bool currently_firing = false;
#endif
static flock_spin_intsafe_t queue_lock = FLOCK_SPIN_INTSAFE_INIT;
static ftimers_id_t next_id = 0;

FERRO_ALWAYS_INLINE size_t parent_index_for_index(size_t index) {
	return (index - 1) / 2;
};

FERRO_ALWAYS_INLINE size_t left_child_index(size_t index) {
	return (index * 2) + 1;
};

FERRO_ALWAYS_INLINE size_t right_child_index(size_t index) {
	return (index * 2) + 2;
};

// needs the queue and backend locks
static bool recalculate_delays_locked(ftimers_backend_timestamp_t timestamp) {
	for (size_t i = 0; i < queue.length; ++i) {
		ftimers_timer_t* timer = &queue.timers[i];
		uint64_t ns = backends[backend]->delta_to_ns(timer->most_recent_timestamp, timestamp);
		if (ns > timer->remaining_delay) {
			timer->remaining_delay = 0;
		} else {
			timer->remaining_delay -= ns;
		}
		timer->most_recent_timestamp = timestamp;
	}
	return queue.length > 0 && (queue.timers[0].remaining_delay == 0 || queue.timers[0].disabled);
};

// needs the queue lock
static void ftimers_priority_queue_remove_locked(void) {
	size_t index = 0;

	// replace the first timer with the last
	queue.timers[0] = queue.timers[--queue.length];

	// and move it down as necessary
	while (true) {
		size_t left_idx = left_child_index(index);
		size_t right_idx = right_child_index(index);
		uint64_t left_delay;
		uint64_t right_delay;
		uint64_t this_delay = queue.timers[index].remaining_delay;

		if (left_idx >= queue.length) {
			// since the left child MUST come first, we know that if we don't have a left child, then we don't have a right one either
			break;
		}

		left_delay = queue.timers[left_idx].remaining_delay;
		right_delay = (right_idx < queue.length) ? queue.timers[right_idx].remaining_delay : UINT64_MAX;

		if (left_delay < this_delay || right_delay < this_delay) {
			ftimers_timer_t tmp;

			if (right_delay < left_delay) {
				tmp = queue.timers[right_idx];
				queue.timers[right_idx] = queue.timers[index];
				queue.timers[index] = tmp;
				index = right_idx;
			} else {
				tmp = queue.timers[left_idx];
				queue.timers[left_idx] = queue.timers[index];
				queue.timers[index] = tmp;
				index = left_idx;
			}
		} else {
			// if our delay is shorter than both of our children's delays, then we're already in the right spot
			break;
		}
	}

	// if the queue is now a fourth of the allocated size, we should shrink it to half its size
	if (queue.size > 4 && queue.length < queue.size / 4) {
		if (fmempool_reallocate(queue.timers, sizeof(ftimers_timer_t) * (queue.size / 2), NULL, (void**)&queue.timers) != ferr_ok) {
			// this should be impossible; shrinking is always possible
			fpanic("failed to shrink timer priority queue");
		}
		queue.size /= 2;
	}
};

static void ftimers_priority_queue_remove(void) {
	flock_spin_intsafe_lock(&queue_lock);
	ftimers_priority_queue_remove_locked();
	flock_spin_intsafe_unlock(&queue_lock);
};

// needs the queue and backend locks.
// note that this function might drop both the queue and backend locks and reacquire them afterwards
// (so that the callback can call ftimers functions).
// this also arms the backend for the next timer (so that if the callback doesn't return to us, we'll still fire the next timer when appropriate).
static void fire_one_locked(void) {
	ftimers_callback_f callback = queue.timers[0].callback;
	void* data = queue.timers[0].data;
	bool disabled = queue.timers[0].disabled;

	ftimers_priority_queue_remove_locked();

	if (queue.length > 0) {
		uint64_t sched_delay = queue.timers[0].remaining_delay;
		backends[backend]->schedule(sched_delay + MIN_SCHED_DELAY_NS);
	}

	if (disabled) {
		// if it's disabled, we're done
		return;
	}

	//__builtin_debugtrap();

	flock_spin_intsafe_unlock(&queue_lock);
	flock_spin_intsafe_unlock(&backend_lock);

	//__builtin_debugtrap();

	callback(data);

	flock_spin_intsafe_lock(&backend_lock);
	flock_spin_intsafe_lock(&queue_lock);
};

// needs the backend and queue locks
static void fire_all_locked(void) {
#if 0
	// someone is already firing all the timers
	// don't start firing in a nested call
	if (currently_firing) {
		return;
	}

	currently_firing = true;
#endif

	while (recalculate_delays_locked(backends[backend]->current_timestamp())) {
		fire_one_locked();
	}

#if 0
	currently_firing = false;
#endif
};

static ftimers_id_t ftimers_priority_queue_add_locked(uint64_t delay, ftimers_callback_f callback, void* data) {
	ftimers_timer_t* new_timer;
	ftimers_id_t id = UINTPTR_MAX;
	size_t index;
	ftimers_backend_timestamp_t timestamp;

	if (queue.length >= queue.size / 2) {
		size_t new_size = 4;

		if (queue.size > 0) {
			new_size = queue.size * 2;
		}

		if (fmempool_reallocate(queue.timers, sizeof(ftimers_timer_t) * new_size, NULL, (void**)&queue.timers) != ferr_ok) {
			goto out;
		}

		queue.size = new_size;
	}

	timestamp = backends[backend]->current_timestamp();

	recalculate_delays_locked(timestamp);

	index = queue.length++;
	new_timer = &queue.timers[index];

	new_timer->id = id = next_id++;
	if (next_id == FTIMERS_ID_INVALID) {
		next_id = 0;
	}
	new_timer->remaining_delay = delay;
	new_timer->most_recent_timestamp = timestamp;
	new_timer->disabled = false;
	new_timer->callback = callback;
	new_timer->data = data;

	new_timer->remaining_delay = delay;

	// now find where it really belongs
	while (index > 0) {
		size_t parent_index = parent_index_for_index(index);
		ftimers_timer_t tmp;

		if (queue.timers[parent_index].remaining_delay < queue.timers[index].remaining_delay) {
			break;
		}

		tmp = queue.timers[parent_index];
		queue.timers[parent_index] = queue.timers[index];
		queue.timers[index] = tmp;

		index = parent_index;
	}

out:
	return id;
};

static ftimers_id_t ftimers_priority_queue_add(uint64_t delay, ftimers_callback_f callback, void* data) {
	ftimers_id_t id;
	flock_spin_intsafe_lock(&queue_lock);
	id = ftimers_priority_queue_add_locked(delay, callback, data);
	flock_spin_intsafe_unlock(&queue_lock);
	return id;
};

void ftimers_backend_fire(void) {
	flock_spin_intsafe_lock(&backend_lock);
	flock_spin_intsafe_lock(&queue_lock);

	fire_all_locked();

	flock_spin_intsafe_unlock(&queue_lock);
	flock_spin_intsafe_unlock(&backend_lock);
};

// takes the backend lock and MAY take the queue lock
ferr_t ftimers_register_backend(const ftimers_backend_t* new_backend) {
	ferr_t status = ferr_ok;
	uint8_t index = 0;

	if (!new_backend || !new_backend->schedule || !new_backend->current_timestamp || !new_backend->delta_to_ns || !new_backend->cancel) {
		status = ferr_invalid_argument;
		goto out_unlocked;
	}

	flock_spin_intsafe_lock(&backend_lock);

	if (backend_count >= MAX_BACKENDS) {
		status = ferr_permanent_outage;
		goto out;
	}

	index = backend_count++;

	backends[index] = new_backend;

	if (index == 0) {
		// if we didn't have a backend, use it
		backend = index;

		fconsole_logf("switching to timer backend \"%s\" (with precision=%uns)\n", new_backend->name, new_backend->precision);

		// we don't need to worry about pre-existing timers because we can't have any in this state!
	} else if (new_backend->precision < backends[backend]->precision) {
		// if this one is more precise, use it
		const ftimers_backend_t* old_backend = backends[index];
		ftimers_backend_timestamp_t timestamp;

		fconsole_logf("switching to timer backend \"%s\" (with precision=%uns)\n", new_backend->name, new_backend->precision);

		// since we're switching backends, we have to switch over the values for any existing timers, so lock the queue
		flock_spin_intsafe_lock(&queue_lock);

		// first, disarm the old backend
		old_backend->cancel();

		// next, get the most up to date values possible
		// calculate our new timestamp here, too, so that they match up as much as possible
		timestamp = new_backend->current_timestamp();
		recalculate_delays_locked(old_backend->current_timestamp());

		// now set the new backend
		backend = index;

		// and switch over the timestamps
		for (size_t i = 0; i < queue.length; ++i) {
			ftimers_timer_t* timer = &queue.timers[i];
			timer->most_recent_timestamp = timestamp;
		}

		// finally, schedule the next-in-line timer (if there is one)
		if (queue.length > 0) {
			uint64_t sched_delay = queue.timers[0].remaining_delay;
			backends[backend]->schedule(sched_delay + MIN_SCHED_DELAY_NS);
		}

		flock_spin_intsafe_unlock(&queue_lock);
	}

out:
	flock_spin_intsafe_unlock(&backend_lock);
out_unlocked:
	return ferr_ok;
};

ferr_t ftimers_oneshot_blocking(uint64_t delay, ftimers_callback_f callback, void* data, ftimers_id_t* out_id) {
	ferr_t status = ferr_ok;
	ftimers_id_t id;

	if (!callback) {
		status = ferr_invalid_argument;
		goto out_unlocked;
	}

	flock_spin_intsafe_lock(&backend_lock);

	if (backend_count == 0) {
		status = ferr_temporary_outage;
		goto out;
	}

	flock_spin_intsafe_lock(&queue_lock);

	id = ftimers_priority_queue_add_locked(delay, callback, data);
	if (id == UINTPTR_MAX) {
		status = ferr_temporary_outage;
		goto out2;
	} else if (out_id) {
		*out_id = id;
	}

	// finally, schedule the next-in-line timer (if there is one)
	if (queue.length > 0) {
		uint64_t sched_delay = queue.timers[0].remaining_delay;
		backends[backend]->schedule(sched_delay + MIN_SCHED_DELAY_NS);
	}

out2:
	flock_spin_intsafe_unlock(&queue_lock);
out:
	flock_spin_intsafe_unlock(&backend_lock);
out_unlocked:
	return status;
};

ferr_t ftimers_cancel(ftimers_id_t id) {
	ferr_t status = ferr_no_such_resource;

	flock_spin_intsafe_lock(&backend_lock);

	if (backend_count == 0) {
		status = ferr_temporary_outage;
		goto out;
	}

	flock_spin_intsafe_lock(&queue_lock);

	for (size_t i = 0; i < queue.length; ++i) {
		if (queue.timers[i].id == id) {
			queue.timers[i].disabled = true;
			status = ferr_ok;

			// the shortest delay was determined by this timer, but it's no longer active.
			// inform the backend about this. `fire_all_locked` will take care of removing this disabled timer
			// and if there are any other timers in the queue, it'll arm the backend with the next appropriate delay.
			if (i == 0) {
				backends[backend]->cancel();
			}
			break;
		}
	}

	// finally, schedule the next-in-line timer (if there is one)
	if (queue.length > 0) {
		uint64_t sched_delay = queue.timers[0].remaining_delay;
		backends[backend]->schedule(sched_delay + MIN_SCHED_DELAY_NS);
	}

	flock_spin_intsafe_unlock(&queue_lock);

out:
	flock_spin_intsafe_unlock(&backend_lock);
out_unlocked:
	return status;
};
