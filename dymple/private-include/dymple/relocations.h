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

#ifndef _DYMPLE_RELOCATIONS_PRIVATE_H_
#define _DYMPLE_RELOCATIONS_PRIVATE_H_

#include <dymple/base.h>
#include <dymple/images.private.h>

DYMPLE_DECLARATIONS_BEGIN;

DYMPLE_STRUCT(dymple_relocation_info) {
	void* rebase_instructions;
	size_t rebase_instructions_size;

	void* bind_instructions;
	size_t bind_instructions_size;

	void* weak_bind_instructions;
	size_t weak_bind_instructions_size;
};

DYMPLE_PACKED_STRUCT(dymple_stub_binding_info) {
	dymple_image_t** image_handle;
	uint64_t lazy_binding_info_offset;
};

DYMPLE_WUR ferr_t dymple_relocate_image(dymple_image_t* image, dymple_relocation_info_t* info);

void* dymple_bind_stub(dymple_stub_binding_info_t* stub_binding_info);

DYMPLE_DECLARATIONS_END;

#endif // _DYMPLE_RELOCATIONS_PRIVATE_H_
