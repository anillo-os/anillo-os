#include <ferro/core/waitq.private.h>

void fwaitq_lock(fwaitq_t* waitq) {
	flock_spin_intsafe_lock(&waitq->lock);
};

void fwaitq_unlock(fwaitq_t* waitq) {
	flock_spin_intsafe_unlock(&waitq->lock);
};

void fwaitq_add_locked(fwaitq_t* waitq, fwaitq_waiter_t* waiter) {
	waiter->prev = waitq->tail;
	waiter->next = NULL;

	if (waiter->prev) {
		waiter->prev->next = waiter;
	}

	if (!waitq->head) {
		waitq->head = waiter;
	}
	waitq->tail = waiter;
};

void fwaitq_remove_locked(fwaitq_t* waitq, fwaitq_waiter_t* waiter) {
	if (waiter == waitq->head) {
		waitq->head = waiter->next;
	}
	if (waiter == waitq->tail) {
		waitq->tail = waiter->prev;
	}

	if (waiter->prev) {
		waiter->prev->next = waiter->next;
	}
	if (waiter->next) {
		waiter->next->prev = waiter->prev;
	}

	waiter->prev = NULL;
	waiter->next = NULL;
};

void fwaitq_waiter_init(fwaitq_waiter_t* waiter, fwaitq_waiter_wakeup_f wakeup, void* data) {
	waiter->prev = NULL;
	waiter->next = NULL;
	waiter->wakeup = wakeup;
	waiter->data = data;
};

void fwaitq_init(fwaitq_t* waitq) {
	waitq->head = NULL;
	waitq->tail = NULL;
	flock_spin_intsafe_init(&waitq->lock);
};

void fwaitq_wait(fwaitq_t* waitq, fwaitq_waiter_t* waiter) {
	fwaitq_lock(waitq);
	fwaitq_add_locked(waitq, waiter);
	fwaitq_unlock(waitq);
};

void fwaitq_wake_many_locked(fwaitq_t* waitq, size_t count) {
	while (waitq->head && count > 0) {
		fwaitq_waiter_t* waiter = waitq->head;

		fwaitq_remove_locked(waitq, waiter);
		fwaitq_unlock(waitq);
		waiter->wakeup(waiter->data);
		fwaitq_lock(waitq);

		--count;
	}
};

void fwaitq_wake_many(fwaitq_t* waitq, size_t count) {
	fwaitq_lock(waitq);
	fwaitq_wake_many_locked(waitq, count);
	fwaitq_unlock(waitq);
};

void fwaitq_wake_specific(fwaitq_t* waitq, fwaitq_waiter_t* waiter) {
	fwaitq_lock(waitq);
	fwaitq_remove_locked(waitq, waiter);
	fwaitq_unlock(waitq);
	waiter->wakeup(waiter->data);
};
