/*
 * This file is part of Anillo OS
 * Copyright (C) 2023 Anillo OS Developers
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

#include <ferro/userspace/uio.h>
#include <ferro/core/mempool.h>
#include <ferro/kasan.h>
#include <libsimple/libsimple.h>
#include <ferro/core/panic.h>
#include <ferro/core/paging.h>

FERRO_NO_KASAN
ferr_t ferro_uio_copy_in(uintptr_t user_address, size_t size, void** out_copy) {
	ferr_t status = ferr_ok;

	if (!fpage_address_is_canonical(user_address)) {
		fpanic("DEBUGGING: NONCANONICAL COPY IN ADDRESS");
	}

	if (user_address >= FERRO_KERNEL_VIRTUAL_START) {
		fpanic("DEBUGGING: USER ADDRESS IS IN KERNEL SPACE");
	}

	status = fmempool_allocate(size, NULL, out_copy);
	if (status != ferr_ok) {
		return status;
	}

	return ferro_uio_copy_in_noalloc(user_address, size, *out_copy);
};

FERRO_NO_KASAN
ferr_t ferro_uio_copy_in_noalloc(uintptr_t user_address, size_t size, void* out_buffer) {
	// TODO: check if the address is valid

	if (!fpage_address_is_canonical(user_address)) {
		fpanic("DEBUGGING: NONCANONICAL COPY IN ADDRESS");
	}

	if (user_address >= FERRO_KERNEL_VIRTUAL_START) {
		fpanic("DEBUGGING: USER ADDRESS IS IN KERNEL SPACE");
	}

	ferro_kasan_copy_unchecked(out_buffer, (const void*)user_address, size);
	return ferr_ok;
};

FERRO_NO_KASAN
ferr_t ferro_uio_copy_out(const void* buffer, size_t size, uintptr_t user_address) {
	// TODO: check if the address is valid

	if (!fpage_address_is_canonical(user_address)) {
		fpanic("DEBUGGING: NONCANONICAL COPY OUT ADDRESS");
	}

	if (user_address >= FERRO_KERNEL_VIRTUAL_START) {
		fpanic("DEBUGGING: USER ADDRESS IS IN KERNEL SPACE");
	}

	ferro_kasan_copy_unchecked((void*)user_address, buffer, size);
	return ferr_ok;
};

void ferro_uio_copy_free(void* copy, size_t size) {
	fpanic_status(fmempool_free(copy));
};

FERRO_NO_KASAN
ferr_t ferro_uio_atomic_load_1_relaxed(uintptr_t user_address, uint8_t* out_value) {
	// TODO: check if the address is valid
	__atomic_load((uint8_t*)user_address, out_value, __ATOMIC_RELAXED);
	return ferr_ok;
};

FERRO_NO_KASAN
ferr_t ferro_uio_atomic_load_8_relaxed(uintptr_t user_address, uint64_t* out_value) {
	// TODO: check if the address is valid
	__atomic_load((uint64_t*)user_address, out_value, __ATOMIC_RELAXED);
	return ferr_ok;
};

FERRO_NO_KASAN
ferr_t ferro_uio_atomic_store_8_relaxed(uintptr_t user_address, uint64_t value) {
	// TODO: check if the address is valid
	__atomic_store((uint64_t*)user_address, &value, __ATOMIC_RELAXED);
	return ferr_ok;
};
