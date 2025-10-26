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

#include <libspooky/structure.private.h>
#include <libspooky/function.h>

static void spooky_structure_destroy(spooky_object_t* obj) {
	spooky_structure_object_t* structure = (void*)obj;

	for (size_t i = 0; i < structure->member_count; ++i) {
		spooky_release(structure->members[i].type);
	}

	sys_object_destroy(obj);
};

static const spooky_object_class_t structure_class = {
	LIBSYS_OBJECT_CLASS_INTERFACE(NULL),
	.destroy = spooky_structure_destroy,
};

const spooky_object_class_t* spooky_object_class_structure(void) {
	return &structure_class;
};

ferr_t spooky_structure_create(size_t total_size, const spooky_structure_member_t* members, size_t member_count, spooky_structure_t** out_structure) {
	ferr_t status = ferr_ok;
	spooky_structure_object_t* structure = NULL;
	size_t retained_members = 0;
	size_t last_offset = 0;
	size_t last_size = 0;

	for (; retained_members < member_count; ++retained_members) {
		if (spooky_object_class(members[retained_members].type) == spooky_object_class_function()) {
			// structures cannot contain member functions; they are pure data types
			status = ferr_invalid_argument;
			goto out;
		}
		if (spooky_retain(members[retained_members].type) != ferr_ok) {
			status = ferr_permanent_outage;
			goto out;
		}
		if (members[retained_members].offset > last_offset) {
			last_offset = members[retained_members].offset;
			last_size = ((spooky_type_object_t*)members[retained_members].type)->byte_size;
		}
	}

	if (total_size < last_offset + last_size) {
		status = ferr_invalid_argument;
		goto out;
	}

	status = sys_object_new(&structure_class, sizeof(*structure) - sizeof(structure->base.object) + (sizeof(*members) * member_count), (void*)&structure);
	if (status != ferr_ok) {
		goto out;
	}

	structure->base.byte_size = total_size;
	structure->base.global = false;
	structure->member_count = member_count;
	simple_memcpy(structure->members, members, sizeof(*members) * member_count);

out:
	if (status == ferr_ok) {
		*out_structure = (void*)structure;
	} else if (structure) {
		spooky_release((void*)structure);
	} else {
		for (size_t i = 0; i < retained_members; ++i) {
			spooky_release(members[i].type);
		}
	}
	return status;
};
