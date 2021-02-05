/**
 * This file is part of Anillo OS
 * Copyright (C) 2020 Anillo OS Developers
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
// framebuffer.c
//
// framebuffer implementation for the kernel that is used in early boot.
//

#include <ferro/core/framebuffer.h>
#include <ferro/bits.h>
#include <libk/libk.h>

#define round_up_div(value, multiple) ({ \
		__typeof__(value) _value = (value); \
		__typeof__(multiple) _multiple = (multiple); \
		(_value + (_multiple - 1)) / _multiple; \
	})

static ferro_fb_info_t* fb_info = NULL;
static ferro_fb_pixel_t black_pixel = {
	.red = 0,
	.green = 0,
	.blue = 0,
};

FERRO_INLINE bool is_within_bounds(size_t x, size_t y) {
	return x < fb_info->width && y < fb_info->height;
};
FERRO_INLINE bool is_within_bounds_rect(const ferro_fb_rect_t* rect) {
	return is_within_bounds(rect->top_left.x, rect->top_left.y) && is_within_bounds(rect->bottom_right.x, rect->bottom_right.y);
};

FERRO_INLINE size_t rect_width(const ferro_fb_rect_t* rect) {
	return rect->bottom_right.x - rect->top_left.x + 1;
};
FERRO_INLINE size_t rect_height(const ferro_fb_rect_t* rect) {
	return rect->bottom_right.y - rect->top_left.y + 1;
};

FERRO_INLINE bool rects_are_equal_size(const ferro_fb_rect_t* left, const ferro_fb_rect_t* right) {
	return rect_width(left) == rect_width(right) && rect_height(left) == rect_height(right);
};

FERRO_INLINE int compare_coords(const ferro_fb_coords_t* left, const ferro_fb_coords_t* right) {
	if (left->x < right->x || left->y < right->y) {
		return -1;
	} else if (left->x > right->x || left->y > right->y) {
		return 1;
	} else {
		return 0;
	}
};

FERRO_INLINE int compare_rects(const ferro_fb_rect_t* left, const ferro_fb_rect_t* right) {
	return compare_coords(&left->top_left, &right->top_left);
};

void ferro_fb_init(ferro_fb_info_t* _fb_info) {
	fb_info = _fb_info;

	if (ferro_fb_available()) {
		// clear the framebuffer
		ferro_fb_rect_t entire_screen = {
			.top_left = {
				.x = 0,
				.y = 0,
			},
			.bottom_right = {
				.x = fb_info->width - 1,
				.y = fb_info->height - 1,
			},
		};
		ferro_fb_set_area_clone(&black_pixel, &entire_screen);
	}
};

bool ferro_fb_available(void) {
	return fb_info != NULL;
};

const ferro_fb_info_t* ferro_fb_get_info(void) {
	return (const ferro_fb_info_t*)fb_info;
};

static void pixel_to_buffer(const ferro_fb_pixel_t* pixel, uint8_t* buffer) {
	uint8_t bytes_per_pixel = round_up_div(fb_info->pixel_bits, 8U);
	uint32_t value =
		((uint32_t)pixel->red << ferro_bits_ctz_u32(fb_info->red_mask)) |
		((uint32_t)pixel->green << ferro_bits_ctz_u32(fb_info->green_mask)) |
		((uint32_t)pixel->blue << ferro_bits_ctz_u32(fb_info->blue_mask))
		;

	if (bytes_per_pixel > 0) {
		buffer[0] =  value & 0x000000ff       ;
	}
	if (bytes_per_pixel > 1) {
		buffer[1] = (value & 0x0000ff00) >>  8;
	}
	if (bytes_per_pixel > 2) {
		buffer[2] = (value & 0x00ff0000) >> 16;
	}
	if (bytes_per_pixel > 3) {
		buffer[3] = (value & 0xff000000) >> 24;
	}
};

static void buffer_to_pixel(const uint8_t* buffer, ferro_fb_pixel_t* pixel) {
	uint8_t bytes_per_pixel = round_up_div(fb_info->pixel_bits, 8U);
	uint32_t value = 0;

	if (bytes_per_pixel > 0) {
		value |= (uint32_t)buffer[0]      ;
	}
	if (bytes_per_pixel > 1) {
		value |= (uint32_t)buffer[1] <<  8;
	}
	if (bytes_per_pixel > 2) {
		value |= (uint32_t)buffer[2] << 16;
	}
	if (bytes_per_pixel > 3) {
		value |= (uint32_t)buffer[3] << 24;
	}

	pixel->red = (value & fb_info->red_mask) >> ferro_bits_ctz_u32(fb_info->red_mask);
	pixel->green = (value & fb_info->green_mask) >> ferro_bits_ctz_u32(fb_info->green_mask);
	pixel->green = (value & fb_info->blue_mask) >> ferro_bits_ctz_u32(fb_info->blue_mask);
};

ferr_t ferro_fb_get_pixel(ferro_fb_pixel_t* pixel, size_t x, size_t y) {
	if (!ferro_fb_available()) {
		return ferr_permanent_outage;
	} else if (x > fb_info->width || y > fb_info->height) {
		return ferr_invalid_parameter;
	} else if (pixel == NULL) {
		return ferr_ok;
	}

	uint8_t bytes_per_pixel = round_up_div(fb_info->pixel_bits, 8U);
	uint8_t* framebuffer = fb_info->base;
	size_t base_index = (fb_info->scan_line_size * y) + (x * bytes_per_pixel);

	buffer_to_pixel(framebuffer + base_index, pixel);

	return ferr_ok;
};

ferr_t ferro_fb_set_pixel(const ferro_fb_pixel_t* pixel, size_t x, size_t y) {
	if (!ferro_fb_available()) {
		return ferr_permanent_outage;
	} else if (x > fb_info->width || y > fb_info->height) {
		return ferr_invalid_parameter;
	} else if (pixel == NULL) {
		return ferr_ok;
	}

	uint8_t bytes_per_pixel = round_up_div(fb_info->pixel_bits, 8U);
	uint8_t* framebuffer = fb_info->base;
	size_t base_index = (fb_info->scan_line_size * y) + (x * bytes_per_pixel);

	pixel_to_buffer(pixel, framebuffer + base_index);

	return ferr_ok;
};

ferr_t ferro_fb_set_area_clone(const ferro_fb_pixel_t* pixel, const ferro_fb_rect_t* area) {
	if (!ferro_fb_available()) {
		return ferr_permanent_outage;
	} else if (!is_within_bounds_rect(area)) {
		return ferr_invalid_parameter;
	}


	size_t height = rect_height(area);
	size_t width = rect_width(area);
	uint8_t bytes_per_pixel = round_up_div(fb_info->pixel_bits, 8U);
	uint8_t* framebuffer = fb_info->base;
	size_t base_index = (fb_info->scan_line_size * area->top_left.y) + (area->top_left.x * bytes_per_pixel);
	uint8_t pixelbuf[bytes_per_pixel];

	pixel_to_buffer(pixel, pixelbuf);

	for (size_t i = 0; i < height; ++i) {
		memclone(framebuffer + base_index + (fb_info->scan_line_size * i), pixelbuf, bytes_per_pixel, width);
	}

	return ferr_ok;
};

ferr_t ferro_fb_move(const ferro_fb_rect_t* old_area, const ferro_fb_rect_t* new_area) {
	if (!ferro_fb_available()) {
		return ferr_permanent_outage;
	} else if (!is_within_bounds_rect(old_area) || !is_within_bounds_rect(new_area) || !rects_are_equal_size(old_area, new_area)) {
		return ferr_invalid_parameter;
	}

	int comparison = compare_rects(old_area, new_area);
	size_t height = rect_height(old_area);
	size_t width = rect_width(old_area);
	uint8_t* framebuffer = fb_info->base;
	uint8_t bytes_per_pixel = round_up_div(fb_info->pixel_bits, 8U);
	size_t old_base_index = (fb_info->scan_line_size * old_area->top_left.y) + (old_area->top_left.x * bytes_per_pixel);
	size_t new_base_index = (fb_info->scan_line_size * new_area->top_left.y) + (new_area->top_left.x * bytes_per_pixel);

	if (comparison == 0) {
		// areas are equal; no-op
	} else if (comparison < 0) {
		// old_area comes before new_area
		// start at the bottom
		for (size_t i = height; i > 0; --i) {
			memmove(framebuffer + new_base_index + (fb_info->scan_line_size * (i - 1)), framebuffer + old_base_index + (fb_info->scan_line_size * (i - 1)), width * bytes_per_pixel);
		}
	} else if (comparison > 0) {
		// new_area comes before old_area
		// start at the top
		for (size_t i = 0; i < height; ++i) {
			memmove(framebuffer + new_base_index + (fb_info->scan_line_size * i), framebuffer + old_base_index + (fb_info->scan_line_size * i), width * bytes_per_pixel);
		}
	}

	return ferr_ok;
};

ferr_t ferro_fb_shift(bool up_if_true, size_t row_count, const ferro_fb_pixel_t* fill_value) {
	if (!ferro_fb_available()) {
		return ferr_permanent_outage;
	} else if (row_count > fb_info->height) {
		row_count = fb_info->height;
	}

	ferr_t status = ferr_ok;
	size_t leftover_height = fb_info->height - row_count;

	if (row_count > 0) {
		ferro_fb_rect_t old_area = {
			.top_left = {
				.x = 0,
				.y = up_if_true ? row_count : 0,
			},
			.bottom_right = {
				.x = fb_info->width - 1,
				.y = up_if_true ? (fb_info->height - 1) : row_count - 1,
			},
		};
		ferro_fb_rect_t new_area = {
			.top_left = {
				.x = 0,
				.y = up_if_true ? 0 : row_count,
			},
			.bottom_right = {
				.x = fb_info->width - 1,
				.y = up_if_true ? leftover_height - 1 : (fb_info->height - 1),
			},
		};

		if ((status = ferro_fb_move(&old_area, &new_area)) != ferr_ok)
			return status;
	}

	if (fill_value != NULL && leftover_height > 0) {
		ferro_fb_rect_t fill_area = {
			.top_left = {
				.x = 0,
				.y = up_if_true ? leftover_height : 0,
			},
			.bottom_right = {
				.x = fb_info->width - 1,
				.y = up_if_true ? (fb_info->height - 1) : leftover_height - 1,
			},
		};
		if ((status = ferro_fb_set_area_clone(fill_value, &fill_area)) != ferr_ok)
			return status;
	}

	return status;
};
