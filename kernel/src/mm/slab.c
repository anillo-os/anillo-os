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

#include <ferro/mm/slab.private.h>
#include <ferro/core/panic.h>
#include <ferro/core/mm.private.h>
#include <ferro/kasan.h>

static ferr_t fslab_allocate_region(fslab_t* slab) {
	ferr_t status = ferr_ok;
	fslab_region_t* phys_region = NULL;
	fslab_element_t* next_elm = NULL;
	fslab_region_t* mapped_region = NULL;

	status = fpage_allocate_physical(1, NULL, (void*)&phys_region, 0);
	if (status != ferr_ok) {
		goto out;
	}

	mapped_region = map_phys_fixed_offset_type(phys_region);

	mapped_region->elements = NULL;

	next_elm = (fslab_element_t*)&phys_region[1];

	while (true) {
		next_elm = (fslab_element_t*)(((uintptr_t)next_elm + (slab->element_alignment - 1)) & ~(slab->element_alignment - 1));

		if ((uintptr_t)next_elm + slab->element_size > (uintptr_t)phys_region + FPAGE_PAGE_SIZE) {
			break;
		}

		map_phys_fixed_offset_type(next_elm)->next = mapped_region->elements;
		mapped_region->elements = next_elm;

		ferro_kasan_poison((uintptr_t)map_phys_fixed_offset(next_elm), slab->element_size);

		next_elm = (fslab_element_t*)((uintptr_t)next_elm + slab->element_size);
	}

	mapped_region->next = slab->regions;
	slab->regions = phys_region;

out:
	if (status != ferr_ok) {
		if (phys_region) {
			FERRO_WUR_IGNORE(fpage_free_physical(phys_region, 1));
		}
	}
	return status;
};

void fslab_destroy(fslab_t* slab) {
	fpanic("TODO: fslab_destroy");
};

ferr_t fslab_allocate(fslab_t* slab, void** out_element) {
	ferr_t status = ferr_ok;
	fslab_region_t* mapped_region = NULL;

	flock_spin_intsafe_lock(&slab->lock);

	for (fslab_region_t* region = slab->regions; region != NULL; region = map_phys_fixed_offset_type(region)->next) {
		mapped_region = map_phys_fixed_offset_type(region);

		if (!mapped_region->elements) {
			continue;
		}

get_elm:
		ferro_kasan_unpoison((uintptr_t)map_phys_fixed_offset(mapped_region->elements), slab->element_size);

		*out_element = map_phys_fixed_offset(mapped_region->elements);
		mapped_region->elements = map_phys_fixed_offset_type(mapped_region->elements)->next;
		goto out;
	}

	status = fslab_allocate_region(slab);
	if (status != ferr_ok) {
		goto out;
	}

	mapped_region = map_phys_fixed_offset_type(slab->regions);
	goto get_elm;

out:
	flock_spin_intsafe_unlock(&slab->lock);
	return status;
};

ferr_t fslab_free(fslab_t* slab, void* element) {
	ferr_t status = ferr_invalid_argument;

	element = unmap_phys_fixed_offset(element);

	flock_spin_intsafe_lock(&slab->lock);

	for (fslab_region_t* region = slab->regions; region != NULL; region = map_phys_fixed_offset_type(region)->next) {
		fslab_region_t* mapped_region = map_phys_fixed_offset_type(region);
		fslab_element_t* mapped_element = NULL;

		if ((uintptr_t)element < (uintptr_t)region || (uintptr_t)element >= (uintptr_t)region + FPAGE_PAGE_SIZE) {
			continue;
		}

		mapped_element = map_phys_fixed_offset(element);

		mapped_element->next = mapped_region->elements;
		mapped_region->elements = element;

		ferro_kasan_poison((uintptr_t)map_phys_fixed_offset(element), slab->element_size);

		// TODO: check if we can free this region

		status = ferr_ok;
		goto out;
	}

out:
	flock_spin_intsafe_unlock(&slab->lock);
	return status;
};
