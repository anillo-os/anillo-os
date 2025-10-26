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
 * Framebuffer implementation.
 */

#include <ferro/core/framebuffer.h>
#include <ferro/core/locks.h>
#include <ferro/bits.h>
#include <libsimple/libsimple.h>
#include <ferro/core/paging.h>
#include <ferro/core/mempool.h>

#define round_up_div(value, multiple) ({ \
		__typeof__(value) _value = (value); \
		__typeof__(multiple) _multiple = (multiple); \
		(_value + (_multiple - 1)) / _multiple; \
	})

static ferro_fb_info_t* fb_info = NULL;
static uint8_t* back_buffer = NULL;
static uint8_t* dirty_rows = NULL;
static size_t dirty_rows_page_count = 0;
static const ferro_fb_pixel_t black_pixel = {
	.red = 0,
	.green = 0,
	.blue = 0,
};

// protects reading from and writing to the framebuffer (not the info)
static flock_spin_intsafe_t fb_lock = FLOCK_SPIN_INTSAFE_INIT;

FERRO_ALWAYS_INLINE bool is_within_bounds(size_t x, size_t y) {
	return x < fb_info->width && y < fb_info->height;
};
FERRO_ALWAYS_INLINE bool is_within_bounds_rect(const ferro_fb_rect_t* rect) {
	return is_within_bounds(rect->top_left.x, rect->top_left.y) && is_within_bounds(rect->bottom_right.x, rect->bottom_right.y);
};

FERRO_ALWAYS_INLINE size_t rect_width(const ferro_fb_rect_t* rect) {
	return rect->bottom_right.x - rect->top_left.x + 1;
};
FERRO_ALWAYS_INLINE size_t rect_height(const ferro_fb_rect_t* rect) {
	return rect->bottom_right.y - rect->top_left.y + 1;
};

FERRO_ALWAYS_INLINE bool rects_are_equal_size(const ferro_fb_rect_t* left, const ferro_fb_rect_t* right) {
	return rect_width(left) == rect_width(right) && rect_height(left) == rect_height(right);
};

FERRO_ALWAYS_INLINE int compare_coords(const ferro_fb_coords_t* left, const ferro_fb_coords_t* right) {
	if (left->x < right->x || left->y < right->y) {
		return -1;
	} else if (left->x > right->x || left->y > right->y) {
		return 1;
	} else {
		return 0;
	}
};

FERRO_ALWAYS_INLINE int compare_rects(const ferro_fb_rect_t* left, const ferro_fb_rect_t* right) {
	return compare_coords(&left->top_left, &right->top_left);
};

/**
 * @pre Must be holding ::fb_lock.
 */
FERRO_ALWAYS_INLINE void mark_dirty(size_t first_row, size_t count) {
	// first, deal with non-multiple-of-8 indicies
	if ((first_row & 0x07) != 0) {
		uint8_t first_row_remainder = first_row & 0x07;
		uint8_t bits_after_start = 8 - first_row_remainder;
		uint8_t bit_count = (count > bits_after_start) ? bits_after_start : count;
		dirty_rows[first_row / 8] |= ((uint8_t)((uint8_t)0xff << (8 - bit_count)) >> (8 - bit_count)) << first_row_remainder;
		first_row += bit_count;
		count -= bit_count;
	}

	// first_row is a multiple of 8 here; count may or may not be
	simple_memset(&dirty_rows[first_row / 8], 0xff, count / 8);
	first_row += count & ~0x07;
	count = count & 0x07;

	// now deal with any leftover (non-multiple-of-8) counts
	if (count > 0) {
		dirty_rows[first_row / 8] |= (uint8_t)((uint8_t)0xff << (8 - count)) >> (8 - count);
	}
};

ferr_t ferro_fb_init(ferro_fb_info_t* _fb_info) {
	ferr_t status = ferr_ok;
	size_t fb_page_count;

	fb_info = _fb_info;

	if (!fb_info) {
		return ferr_ok;
	}

	fb_info->total_byte_size = fb_info->scan_line_size * fb_info->height;
	fb_info->bytes_per_pixel = round_up_div(fb_info->pixel_bits, 8U);

	fb_page_count = fpage_round_up_to_page_count(fb_info->total_byte_size);

	dirty_rows_page_count = fpage_round_up_to_page_count(round_up_div(fb_info->height, 8));

	// prebound since we're called before interrupts are enabled
	status = fpage_space_allocate(fpage_space_kernel(), dirty_rows_page_count, (void*)&dirty_rows, fpage_flag_prebound);
	if (status != ferr_ok) {
		fb_info = NULL;
		return status;
	}

	// map the framebuffer
	status = fpage_map_kernel_any(fb_info->base, fb_page_count, &fb_info->base, 0);
	if (status != ferr_ok) {
		FERRO_WUR_IGNORE(fpage_space_free(fpage_space_kernel(), dirty_rows, dirty_rows_page_count));
		fb_info = NULL;
		return status;
	}

	// allocate a back buffer to perform double buffering
	// (needs to be prebound since we're called before interrupts are enabled)
	status = fpage_allocate_kernel(fb_page_count, (void*)&back_buffer, fpage_flag_prebound);
	if (status != ferr_ok) {
		FERRO_WUR_IGNORE(fpage_space_free(fpage_space_kernel(), dirty_rows, dirty_rows_page_count));
		FERRO_WUR_IGNORE(fpage_unmap_kernel(fb_info->base, fb_page_count));
		fb_info = NULL;
		return status;
	}

	// clear the framebuffer, back buffer, and dirty row bitmap
	simple_memset(fb_info->base, 0, fb_info->total_byte_size);
	simple_memset(back_buffer, 0, fb_info->total_byte_size);
	simple_memset(dirty_rows, 0, round_up_div(fb_info->height, 8));

	return status;
};

bool ferro_fb_available(void) {
	return fb_info != NULL;
};

const ferro_fb_info_t* ferro_fb_get_info(void) {
	return (const ferro_fb_info_t*)fb_info;
};

static void pixel_to_buffer(const ferro_fb_pixel_t* pixel, uint8_t* buffer) {
	uint32_t value =
		((uint32_t)pixel->red << ferro_bits_ctz_u32(fb_info->red_mask)) |
		((uint32_t)pixel->green << ferro_bits_ctz_u32(fb_info->green_mask)) |
		((uint32_t)pixel->blue << ferro_bits_ctz_u32(fb_info->blue_mask))
		;

	if (fb_info->bytes_per_pixel > 0) {
		buffer[0] =  value & 0x000000ff       ;
	}
	if (fb_info->bytes_per_pixel > 1) {
		buffer[1] = (value & 0x0000ff00) >>  8;
	}
	if (fb_info->bytes_per_pixel > 2) {
		buffer[2] = (value & 0x00ff0000) >> 16;
	}
	if (fb_info->bytes_per_pixel > 3) {
		buffer[3] = (value & 0xff000000) >> 24;
	}
};

static void buffer_to_pixel(const uint8_t* buffer, ferro_fb_pixel_t* pixel) {
	uint32_t value = 0;

	if (fb_info->bytes_per_pixel > 0) {
		value |= (uint32_t)buffer[0]      ;
	}
	if (fb_info->bytes_per_pixel > 1) {
		value |= (uint32_t)buffer[1] <<  8;
	}
	if (fb_info->bytes_per_pixel > 2) {
		value |= (uint32_t)buffer[2] << 16;
	}
	if (fb_info->bytes_per_pixel > 3) {
		value |= (uint32_t)buffer[3] << 24;
	}

	pixel->red = (value & fb_info->red_mask) >> ferro_bits_ctz_u32(fb_info->red_mask);
	pixel->green = (value & fb_info->green_mask) >> ferro_bits_ctz_u32(fb_info->green_mask);
	pixel->green = (value & fb_info->blue_mask) >> ferro_bits_ctz_u32(fb_info->blue_mask);
};

ferr_t ferro_fb_get_pixel(ferro_fb_pixel_t* pixel, size_t x, size_t y) {
	if (!ferro_fb_available()) {
		return ferr_temporary_outage;
	} else if (x > fb_info->width || y > fb_info->height) {
		return ferr_invalid_argument;
	} else if (pixel == NULL) {
		return ferr_ok;
	}

	size_t base_index = (fb_info->scan_line_size * y) + (x * fb_info->bytes_per_pixel);

	flock_spin_intsafe_lock(&fb_lock);
	buffer_to_pixel(back_buffer + base_index, pixel);
	flock_spin_intsafe_unlock(&fb_lock);

	return ferr_ok;
};

ferr_t ferro_fb_set_pixel(const ferro_fb_pixel_t* pixel, size_t x, size_t y) {
	if (!ferro_fb_available()) {
		return ferr_temporary_outage;
	} else if (x > fb_info->width || y > fb_info->height) {
		return ferr_invalid_argument;
	} else if (pixel == NULL) {
		return ferr_ok;
	}

	size_t base_index = (fb_info->scan_line_size * y) + (x * fb_info->bytes_per_pixel);

	flock_spin_intsafe_lock(&fb_lock);
	pixel_to_buffer(pixel, back_buffer + base_index);
	mark_dirty(y, 1);
	flock_spin_intsafe_unlock(&fb_lock);

	return ferr_ok;
};

ferr_t ferro_fb_set_area_clone(const ferro_fb_pixel_t* pixel, const ferro_fb_rect_t* area) {
	if (!ferro_fb_available()) {
		return ferr_temporary_outage;
	} else if (!is_within_bounds_rect(area)) {
		return ferr_invalid_argument;
	}

	size_t height = rect_height(area);
	size_t width = rect_width(area);
	size_t base_index = (fb_info->scan_line_size * area->top_left.y) + (area->top_left.x * fb_info->bytes_per_pixel);
	uint8_t pixelbuf[fb_info->bytes_per_pixel];

	pixel_to_buffer(pixel, pixelbuf);

	flock_spin_intsafe_lock(&fb_lock);
	for (size_t i = 0; i < height; ++i) {
		simple_memclone(back_buffer + base_index + (fb_info->scan_line_size * i), pixelbuf, fb_info->bytes_per_pixel, width);
	}

	mark_dirty(area->top_left.y, height);

	flock_spin_intsafe_unlock(&fb_lock);

	return ferr_ok;
};

// TODO: optimize this
ferr_t ferro_fb_move(const ferro_fb_rect_t* old_area, const ferro_fb_rect_t* new_area) {
	if (!ferro_fb_available()) {
		return ferr_temporary_outage;
	} else if (!is_within_bounds_rect(old_area) || !is_within_bounds_rect(new_area) || !rects_are_equal_size(old_area, new_area)) {
		return ferr_invalid_argument;
	}

	int comparison = compare_rects(old_area, new_area);
	size_t height = rect_height(old_area);
	size_t width = rect_width(old_area);
	size_t old_base_index = (fb_info->scan_line_size * old_area->top_left.y) + (old_area->top_left.x * fb_info->bytes_per_pixel);
	size_t new_base_index = (fb_info->scan_line_size * new_area->top_left.y) + (new_area->top_left.x * fb_info->bytes_per_pixel);

	if (comparison == 0) {
		// areas are equal; no-op
	} else {
		flock_spin_intsafe_lock(&fb_lock);

		if (comparison < 0) {
			// old_area comes before new_area
			// start at the bottom
			for (size_t i = height; i > 0; --i) {
				simple_memmove(back_buffer + new_base_index + (fb_info->scan_line_size * (i - 1)), back_buffer + old_base_index + (fb_info->scan_line_size * (i - 1)), width * fb_info->bytes_per_pixel);
			}
		} else if (comparison > 0) {
			// new_area comes before old_area
			// start at the top
			for (size_t i = 0; i < height; ++i) {
				simple_memmove(back_buffer + new_base_index + (fb_info->scan_line_size * i), back_buffer + old_base_index + (fb_info->scan_line_size * i), width * fb_info->bytes_per_pixel);
			}
		}

		mark_dirty(new_area->top_left.y, height);

		flock_spin_intsafe_unlock(&fb_lock);
	}

	return ferr_ok;
};

ferr_t ferro_fb_shift(bool up_if_true, size_t row_count, const ferro_fb_pixel_t* fill_value) {
	if (!ferro_fb_available()) {
		return ferr_temporary_outage;
	} else if (row_count == 0) {
		return ferr_ok;
	} else if (row_count > fb_info->height) {
		row_count = fb_info->height;
	}

	ferr_t status = ferr_ok;
	size_t leftover_height = fb_info->height - row_count;
	size_t old_base_index = fb_info->scan_line_size * (up_if_true ? row_count : 0);
	size_t new_base_index = fb_info->scan_line_size * (up_if_true ? 0 : row_count);
	size_t fill_base_index = fb_info->scan_line_size * (up_if_true ? leftover_height : 0);
	uint8_t pixelbuf[fb_info->bytes_per_pixel];

	if (fill_value != NULL) {
		pixel_to_buffer(fill_value, pixelbuf);
	}

	flock_spin_intsafe_lock(&fb_lock);

	simple_memmove(back_buffer + new_base_index, back_buffer + old_base_index, fb_info->scan_line_size * leftover_height);

	mark_dirty((up_if_true ? 0 : row_count), leftover_height);

	if (fill_value != NULL) {

		// first, fill in the first row
		simple_memclone(back_buffer + fill_base_index, pixelbuf, fb_info->bytes_per_pixel, fb_info->width);

		// now use it to fill in the other rows as necessary
		// (this allows us to copy in bigger chunks, which is more efficient)
		simple_memclone(back_buffer + fill_base_index + fb_info->scan_line_size, back_buffer + fill_base_index, fb_info->scan_line_size, row_count - 1);

		mark_dirty((up_if_true ? leftover_height : 0), row_count);
	}

	flock_spin_intsafe_unlock(&fb_lock);

	return status;
};

ferr_t ferro_fb_flush(void) {
	if (!ferro_fb_available()) {
		return ferr_temporary_outage;
	}

	ferr_t status = ferr_ok;

	flock_spin_intsafe_lock(&fb_lock);

	for (size_t i = 0; i < fb_info->height; /* handled in the body */) {
		uint8_t val = dirty_rows[i / 8];

		if (val == 0) {
			// we can skip all 8 rows; i is guaranteed to be a multiple of 8 here
			i += 8;
			continue;
		}

		if ((val & (1 << (i & 0x07))) == 0) {
			++i;
			continue;
		}

		//
		// find how long this region of dirty rows is
		//
		size_t len = 1;
		size_t orig_i = i;

		++i;

		while (i < fb_info->height && (i & 0x07) != 0) {
			if ((dirty_rows[i / 8] & (1 << (i & 0x07))) == 0) {
				break;
			}
			++len;
			++i;
		}

		while (i < fb_info->height && dirty_rows[i / 8] == 0xff) {
			len += 8;
			i += 8;
		}

		while (i < fb_info->height && (i & 0x07) != 0) {
			if ((dirty_rows[i / 8] & (1 << (i & 0x07))) == 0) {
				break;
			}
			++len;
			++i;
		}

		size_t base_index = fb_info->scan_line_size * orig_i;
		simple_memcpy(fb_info->base + base_index, back_buffer + base_index, fb_info->scan_line_size * len);
	}

	simple_memset(dirty_rows, 0, round_up_div(fb_info->height, 8));

	flock_spin_intsafe_unlock(&fb_lock);

	return status;
};

ferr_t ferro_fb_handoff(fpage_mapping_t** out_mapping) {
	size_t fb_page_count = 0;
	void* fb_phys = NULL;
	fpage_mapping_t* mapping = NULL;
	ferr_t status = ferr_ok;

	if (!ferro_fb_available()) {
		status = ferr_permanent_outage;
		goto out;
	}

	fb_page_count = fpage_round_up_to_page_count(fb_info->total_byte_size);
	fb_phys = (void*)fpage_virtual_to_physical((uintptr_t)fb_info->base);

	fassert((uintptr_t)fb_phys != UINTPTR_MAX);

	status = fpage_mapping_new(fb_page_count, fpage_mapping_flag_zero, &mapping);
	if (status != ferr_ok) {
		goto out;
	}

	status = fpage_mapping_bind(mapping, 0, fb_page_count, fb_phys, 0);
	if (status != ferr_ok) {
		goto out;
	}

	*out_mapping = mapping;
	mapping = NULL;

	// we can no longer use this framebuffer
	flock_spin_intsafe_lock(&fb_lock);
	fb_info = NULL;
	flock_spin_intsafe_unlock(&fb_lock);

	FERRO_WUR_IGNORE(fpage_space_free(fpage_space_kernel(), dirty_rows, dirty_rows_page_count));
	FERRO_WUR_IGNORE(fpage_space_free(fpage_space_kernel(), back_buffer, fb_page_count));

out:
	if (mapping) {
		fpage_mapping_release(mapping);
	}
	return status;
};
