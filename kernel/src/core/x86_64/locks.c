#include <ferro/core/locks.h>
#include <immintrin.h>

void flock_spin_init(flock_spin_t* lock) {
	lock->flag = 0;
};

void flock_spin_lock(flock_spin_t* lock) {
	while (__atomic_test_and_set(&lock->flag, __ATOMIC_ACQUIRE)) {
		_mm_pause();
	}
};

bool flock_spin_try_lock(flock_spin_t* lock) {
	return !__atomic_test_and_set(&lock->flag, __ATOMIC_ACQUIRE);
};

void flock_spin_unlock(flock_spin_t* lock) {
	__atomic_clear(&lock->flag, __ATOMIC_RELEASE);
};

void flock_spin_intsafe_init(flock_spin_intsafe_t* lock) {
	flock_spin_init(&lock->base);
};

void flock_spin_intsafe_lock(flock_spin_intsafe_t* lock) {
	fint_disable();
	flock_spin_intsafe_lock_unsafe(lock);
};

void flock_spin_intsafe_lock_unsafe(flock_spin_intsafe_t* lock) {
	return flock_spin_lock(&lock->base);
};

bool flock_spin_intsafe_try_lock(flock_spin_intsafe_t* lock) {
	bool acquired;

	fint_disable();

	acquired = flock_spin_intsafe_try_lock_unsafe(lock);

	if (!acquired) {
		fint_enable();
	}

	return acquired;
};

bool flock_spin_intsafe_try_lock_unsafe(flock_spin_intsafe_t* lock) {
	return flock_spin_try_lock(&lock->base);
};

void flock_spin_intsafe_unlock(flock_spin_intsafe_t* lock) {
	flock_spin_intsafe_unlock_unsafe(lock);
	fint_enable();
};

void flock_spin_intsafe_unlock_unsafe(flock_spin_intsafe_t* lock) {
	return flock_spin_unlock(&lock->base);
};
