#include <ferro/core/workers.h>
#include <ferro/core/locks.h>
#include <ferro/core/mempool.h>
#include <ferro/core/scheduler.h>
#include <ferro/core/threads.h>
#include <ferro/core/panic.h>
#include <ferro/core/cpu.h>

#include <stdatomic.h>

FERRO_ENUM(uint8_t, fworker_state) {
	fworker_state_pending,
	fworker_state_cancelled,
	fworker_state_running,
	fworker_state_complete,
};

FERRO_STRUCT_FWD(fworker_queue);

FERRO_STRUCT(fworker) {
	fworker_t* prev;
	fworker_t* next;
	fworker_queue_t* queue;
	uint64_t reference_count;
	fworker_f function;
	void* data;
	flock_spin_intsafe_t state_lock;
	fworker_state_t state;
};

FERRO_STRUCT(fworker_queue) {
	flock_spin_intsafe_t lock;
	fworker_t* head;
	fworker_t* tail;
	size_t length;
	fthread_t* thread;

	// used by the worker thread to sleep until more workers are added
	flock_semaphore_t semaphore;
};

static fworker_queue_t** worker_queues = NULL;
static size_t worker_queue_count = 0;

static void worker_thread_runner(void* data);


// the queue's lock MUST be held
static void fworker_queue_push_locked(fworker_queue_t* queue, fworker_t* worker) {
	worker->prev = queue->tail;
	worker->next = NULL;
	worker->queue = queue;

	if (!queue->head) {
		queue->head = worker;
	}
	queue->tail = worker;

	++queue->length;

	flock_semaphore_up(&queue->semaphore);
};

// the queue's lock must NOT be held
static void fworker_queue_push(fworker_queue_t* queue, fworker_t* worker) {
	flock_spin_intsafe_lock(&queue->lock);
	fworker_queue_push_locked(queue, worker);
	flock_spin_intsafe_unlock(&queue->lock);
};

// the queue's lock MUST be held
static void fworker_queue_remove_locked(fworker_queue_t* queue, fworker_t* worker) {
	if (worker->prev) {
		worker->prev->next = worker->next;
	} else {
		queue->head = worker->next;
	}

	if (worker->next) {
		worker->next->prev = worker->prev;
	} else {
		queue->tail = worker->prev;
	}

	worker->prev = NULL;
	worker->next = NULL;
	worker->queue = NULL;

	--queue->length;
};

// the queue's lock must NOT be held
static void fworker_queue_remove(fworker_queue_t* queue, fworker_t* worker) {
	flock_spin_intsafe_lock(&queue->lock);
	fworker_queue_remove_locked(queue, worker);
	flock_spin_intsafe_unlock(&queue->lock);
};

// the queue's lock must NOT be held
static fworker_t* fworker_queue_pop(fworker_queue_t* queue) {
	fworker_t* result = NULL;
	flock_spin_intsafe_lock(&queue->lock);
	if (queue->head) {
		result = queue->head;
		fworker_queue_remove_locked(queue, result);
	}
	flock_spin_intsafe_unlock(&queue->lock);
	return result;
};

ferr_t fworker_new(fworker_f worker_function, void* data, fworker_t** out_worker) {
	fworker_t* worker = NULL;

	if (!worker_function || !out_worker) {
		return ferr_invalid_argument;
	}

	if (fmempool_allocate(sizeof(fworker_t), NULL, (void**)&worker) != ferr_ok) {
		return ferr_temporary_outage;
	}

	worker->prev = NULL;
	worker->next = NULL;
	worker->reference_count = 1;
	worker->function = worker_function;
	worker->data = data;
	worker->state = fworker_state_cancelled;
	flock_spin_intsafe_init(&worker->state_lock);

	*out_worker = worker;

	return ferr_ok;
};

static void fworker_destroy(fworker_t* worker) {
	if (fmempool_free(worker) != ferr_ok) {
		fpanic("Failed to free worker instance structure");
	}
};

ferr_t fworker_retain(fworker_t* worker) {
	if (__atomic_fetch_add(&worker->reference_count, 1, __ATOMIC_RELAXED) == 0) {
		return ferr_permanent_outage;
	}

	return ferr_ok;
};

void fworker_release(fworker_t* worker) {
	if (__atomic_sub_fetch(&worker->reference_count, 1, __ATOMIC_ACQ_REL) != 0) {
		return;
	}

	fworker_destroy(worker);
};

static fworker_queue_t* fworker_queue_new(void) {
	fworker_queue_t* queue = NULL;

	if (fmempool_allocate(sizeof(fworker_queue_t), NULL, (void**)&queue) != ferr_ok) {
		return NULL;
	}

	queue->head = NULL;
	queue->tail = NULL;
	queue->length = 0;
	flock_spin_intsafe_init(&queue->lock);
	flock_semaphore_init(&queue->semaphore, 0);

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
	worker_queue_count = fcpu_count();

	if (fmempool_allocate(sizeof(fthread_t*) * worker_queue_count, NULL, (void*)&worker_queues) != ferr_ok) {
		fpanic("Failed to allocate worker queue pointer array");
	}

	for (size_t i = 0; i < worker_queue_count; ++i) {
		worker_queues[i] = fworker_queue_new();
		if (!worker_queues[i]) {
			fpanic("Failed to create a new worker queue");
		}
	}
};

// very similar to the scheduler's `find_lightest_load`
// returns with the queue lock held
static fworker_queue_t* find_lightest_load(void) {
	fworker_queue_t* result = NULL;
	for (size_t i = 0; i < worker_queue_count; ++i) {
		size_t prev_count = result ? result->length : SIZE_MAX;
		if (result) {
			flock_spin_intsafe_unlock(&result->lock);
		}
		// yes, dropping the previous one's lock before acquire this one's lock means the count might've changed.
		// however, if we hold the lock, we can run into deadlocks; so let's prefer to be a little bit inaccurate rather than frozen.
		flock_spin_intsafe_lock(&worker_queues[i]->lock);
		if (prev_count > worker_queues[i]->length) {
			result = worker_queues[i];
		}
	}
	return result;
};

ferr_t fworker_schedule(fworker_t* worker) {
	fworker_queue_t* queue;

	if (!worker) {
		return ferr_invalid_argument;
	}

	flock_spin_intsafe_lock(&worker->state_lock);

	if (worker->state != fworker_state_complete && worker->state != fworker_state_cancelled) {
		flock_spin_intsafe_unlock(&worker->state_lock);
		return ferr_invalid_argument;
	}

	worker->state = fworker_state_pending;

	flock_spin_intsafe_unlock(&worker->state_lock);

	if (fworker_retain(worker) != ferr_ok) {
		return ferr_permanent_outage;
	}

	queue = find_lightest_load();
	if (!queue) {
		fpanic("Failed to find worker queue with lightest load (this is impossible)");
	}

	fworker_queue_push_locked(queue, worker);

	flock_spin_intsafe_unlock(&queue->lock);

	return ferr_ok;
};

ferr_t fworker_cancel(fworker_t* worker) {
	if (!worker) {
		return ferr_invalid_argument;
	}

	flock_spin_intsafe_lock(&worker->state_lock);

	if (worker->state != fworker_state_pending) {
		flock_spin_intsafe_unlock(&worker->state_lock);
		return ferr_already_in_progress;
	}

	worker->state = fworker_state_cancelled;

	flock_spin_intsafe_unlock(&worker->state_lock);

	fworker_queue_remove(worker->queue, worker);

	fworker_release(worker);

	return ferr_ok;
};

static void worker_thread_runner(void* data) {
	fworker_queue_t* queue = data;

	while (true) {
		fworker_t* worker;

		// wait until we have something to work with
		flock_semaphore_down(&queue->semaphore);

		worker = fworker_queue_pop(queue);

		// no worker? no problem.
		if (!worker) {
			continue;
		}

		flock_spin_intsafe_lock(&worker->state_lock);

		// if it's not pending, it's:
		//   * cancelled, so we shouldn't do anything with it
		//   * running? which would be weird, because that means someone else ran it.
		//   * complete? which would also be weird, because it would also mean someone else ran it.
		if (worker->state != fworker_state_pending) {
			// in any case, if we can't run it, release it and try again for another worker instance
			flock_spin_intsafe_unlock(&worker->state_lock);
			fworker_release(worker);
			continue;
		}

		// okay, we're about to start running it ourselves, so mark it as such
		worker->state = fworker_state_running;
		flock_spin_intsafe_unlock(&worker->state_lock);

		// now let's run it
		worker->function(worker->data);

		// okay, we're done running it, so mark it as such
		flock_spin_intsafe_lock(&worker->state_lock);
		worker->state = fworker_state_complete;
		flock_spin_intsafe_unlock(&worker->state_lock);

		// okay, we don't need the worker anymore so we can release it
		fworker_release(worker);

		// great, now we'll loop around again and try to process another worker instance
	}
};
