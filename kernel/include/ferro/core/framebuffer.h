/*
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

#ifndef _FERRO_CORE_FRAMEBUFFER_H_
#define _FERRO_CORE_FRAMEBUFFER_H_

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include <ferro/base.h>
#include <ferro/error.h>

FERRO_DECLARATIONS_BEGIN;

typedef struct ferro_fb_info ferro_fb_info_t;
struct ferro_fb_info {
	void* base;
	size_t width;
	size_t height;
	size_t scan_line_size;
	size_t pixel_bits;
	uint32_t red_mask;
	uint32_t green_mask;
	uint32_t blue_mask;
	uint32_t other_mask;
};

typedef struct ferro_fb_pixel ferro_fb_pixel_t;
struct ferro_fb_pixel {
	uint8_t red;
	uint8_t green;
	uint8_t blue;
};

typedef struct ferro_fb_coords ferro_fb_coords_t;
struct ferro_fb_coords {
	// X coordinate
	size_t x;

	// Y coordinate
	size_t y;
};

typedef struct ferro_fb_rect ferro_fb_rect_t;
struct ferro_fb_rect {
	// Coordinates of the top-leftmost pixel of the area, inclusive.
	ferro_fb_coords_t top_left;

	// Coordinates of bottom-rightmost pixel of the area, inclusive.
	ferro_fb_coords_t bottom_right;
};

/**
 * Initializes the framebuffer subsystem. Called on kernel startup.
 * 
 * @param fb_info Pointer to structure containing information about the graphics framebuffer. May be NULL if no framebuffer is available.
 */
void ferro_fb_init(ferro_fb_info_t* fb_info);

/**
 * Determines whether there is a framebuffer available.
 * 
 * @returns `true` if there's a framebuffer available, `false` otherwise.
 */
bool ferro_fb_available(void);

/**
 * Retrieves information about the current framebuffer.
 *
 * @returns A pointer to a read-only structure containing information about the current framebuffer. May be `NULL` if no framebuffer is available.
 */
const ferro_fb_info_t* ferro_fb_get_info(void);

/**
 * Retrieves the values of the pixel in the framebuffer at (x, y) and writes them into the given pixel structure.
 *
 * @param pixel Pointer to a pixel structure to copy the values of the framebuffer pixel into. If this is `NULL`, the function is a no-op.
 * @param x     X coordinate of the pixel to retrieve in the framebuffer. This corresponds to the pixel's column.
 * @param y     Y coordinate of the pixel to retrieve in the framebuffer. This corresponds to the pixel's row.
 *
 * Return values:
 * @retval ferr_ok                Successfully retrieved pixel information into `pixel`.
 * @retval ferr_invalid_argument  Either `x` or `y` was out-of-bounds for the framebuffer. `pixel` remains unmodified and the framebuffer is never accessed.
 * @retval ferr_temporary_outage  No framebuffer is available. `pixel` remains unmodified.
 */
FERRO_WUR ferr_t ferro_fb_get_pixel(ferro_fb_pixel_t* pixel, size_t x, size_t y);

/**
 * Assigns the values in the given pixel structure to the pixel in the framebuffer at (x, y).
 * 
 * @param pixel Read-only pointer to a pixel structure for the new values of the pixel. If this is `NULL`, the function is a no-op.
 * @param x     X coordinate of the pixel to overwrite in the framebuffer. This corresponds to the pixel's column.
 * @param y     Y coordinate of the pixel to overwrite in the framebuffer. This corresponds to the pixel's row.
 *
 * Return values:
 * @retval ferr_ok                Successfully assigned pixel information from `pixel` into the framebuffer.
 * @retval ferr_invalid_argument  Either `x` or `y` was out-of-bounds for the framebuffer. The framebuffer remains unmodified and `pixel` is never accessed.
 * @retval ferr_temporary_outage  No framebuffer is available. `pixel` is never accessed.
 */
FERRO_WUR ferr_t ferro_fb_set_pixel(const ferro_fb_pixel_t* pixel, size_t x, size_t y);

/**
 * Assigns the values in the given pixel structure to the pixels in the area bound by (start_x, start_y) to (end_x, end_y), inclusive.
 *
 * @param pixel Read-only pointer to a single pixel structure whose values will overwrite the values of all the pixel in the given area of the framebuffer.
 * @param area  Pointer to a structure describing the area to be overwritten.
 *
 * Return values:
 * @retval ferr_ok                Successfully assigned pixel information from `pixel` into the framebuffer.
 * @retval ferr_invalid_argument  The area was out-of-bounds for the framebuffer. The framebuffer remains unmodified and `pixel` is never accessed.
 * @retval ferr_temporary_outage  No framebuffer is available. `pixel` and `area` are never accessed.
 */
FERRO_WUR ferr_t ferro_fb_set_area_clone(const ferro_fb_pixel_t* pixel, const ferro_fb_rect_t* area);

/**
 * Copies the area described by `old_area` to the location described by `new_area`. The areas MUST be simple translations of each other.
 *
 * @param old_area Pointer to a structure describing the area to copy pixels from.
 * @param new_area Pointer to a structure describing the area to copy pixels to.
 *
 * Return values:
 * @retval ferr_ok                Successfully copied the pixels from the area described by `old_area` to the area described by `new_area`.
 * @retval ferr_invalid_argument  One or both areas were out-of-bounds for the framebuffer, OR the areas were not simple translations of each other.
 * @retval ferr_temporary_outage  No framebuffer is available. `old_area` and `new_area` are never accessed.
 */
FERRO_WUR ferr_t ferro_fb_move(const ferro_fb_rect_t* old_area, const ferro_fb_rect_t* new_area);

/**
 * Shifts the entire framebuffer up or down by the given number of rows, optionally filling in the cleared rows.
 *
 * @param up_if_true If this is `true`, the framebuffer is shifted up. Otherwise, if `false`, it's shifted down.
 * @param row_count  Number of rows to shift the framebuffer up or down by.
 * @param fill_value Optional pointer to a pixel structure to fill in the rows left "empty" after the shift is done. If this is `NULL`, the "empty" rows are left unmodified.
 *
 * Return values:
 * @retval ferr_ok               Successfully shifted framebuffer rows up or down.
 * @retval ferr_temporary_outage No framebuffer is available. `fill_value` is never accessed.
 */
FERRO_WUR ferr_t ferro_fb_shift(bool up_if_true, size_t row_count, const ferro_fb_pixel_t* fill_value);

FERRO_DECLARATIONS_END;

#endif // _FERRO_CORE_FRAMEBUFFER_H_
