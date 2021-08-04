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
// console.c
//
// console implementation for the kernel that starts working on early boot and continues later on
//

#include <stdint.h>
#include <stdbool.h>

#include <ferro/core/console.h>
#include <ferro/core/framebuffer.h>
#include <ferro/core/mempool.h>
#include <ferro/core/panic.h>
#include <libk/libk.h>

#include <gen/ferro/font.h>

#define PSF_FLAG_UNICODE 1

FERRO_PACKED_STRUCT(ferro_console_font) {
	uint32_t magic;
	uint32_t version;
	uint32_t header_size;
	uint32_t flags;
	uint32_t glyph_count;
	uint32_t glyph_size;
	uint32_t glyph_height;
	uint32_t glyph_width;
	uint8_t glyphs[];
};

ferro_console_font_t* font = (ferro_console_font_t*)font_data;

/**
 * Translates a single character from UTF-8 encoding into UTF-32 encoding.
 *
 * @param char_seq        UTF-8 representation of the character to transcode.
 * @param available_bytes Number of bytes available to be read from `char_seq`.
 * @param utf8_length     A pointer to a variable in which the number of bytes the UTF-8 representation occupies will be written. May be `NULL`. If provided, it is always written to with the required number of bytes, regardless of success or failure.
 *
 * @returns UINT32_MAX if an error occurred (i.e. invalid sequence or insufficient bytes), or the UTF-32 representation of the character otherwise.
 */
static uint32_t utf8_to_utf32(const char* char_seq, size_t available_bytes, size_t* utf8_length) {
	if (available_bytes == 0) {
		if (utf8_length)
			*utf8_length = 0;
		return UINT32_MAX;
	}

	uint32_t utf32_char = UINT32_MAX;
	uint8_t required_length = 0;

	if (available_bytes > 0) {
		uint8_t first_char = char_seq[0];

		if (first_char & 0x80) {
			if ((first_char & 0x20) == 0) {
				// 2 bytes
				required_length = 2;
				if (available_bytes < required_length)
					goto out;
				utf32_char = ((first_char & 0x1f) << 6) | (char_seq[1] & 0x3f);
			} else if ((first_char & 0x10) == 0) {
				// 3 bytes
				required_length = 3;
				if (available_bytes < required_length)
					goto out;
				utf32_char = ((first_char & 0x0f) << 12) | ((char_seq[1] & 0x3f) << 6) | (char_seq[2] & 0x3f);
			} else if ((first_char & 0x08) == 0) {
				// 4 bytes
				required_length = 4;
				if (available_bytes < required_length)
					goto out;
				utf32_char = ((first_char & 0x07) << 18) | ((char_seq[1] & 0x3f) << 12) | ((char_seq[2] & 0x3f) << 6) | (char_seq[3] & 0x3f);
			} else {
				// more than 4 bytes???
				goto out;
			}
		} else {
			required_length = 1;
			utf32_char = first_char;
		}
	}

out:
	if (utf8_length)
		*utf8_length = required_length;
	return utf32_char;
};

static ferr_t fconsole_put_utf32_char(uint32_t unichar, size_t x, size_t y, const ferro_fb_pixel_t* foreground, const ferro_fb_pixel_t* background) {
	uint16_t index = (font->flags & PSF_FLAG_UNICODE) ? unicode_map[unichar] : (uint16_t)(unichar & 0xffff);
	uint8_t* glyph;

	if (index > font->glyph_count) {
		index = 0;
	}

	glyph = &font->glyphs[index * font->glyph_size];

	for (uint32_t glyph_y = 0; glyph_y < font->glyph_height; ++glyph_y) {
		uint8_t* row = &glyph[glyph_y * ((font->glyph_width + 7) / 8)];

		for (uint32_t glyph_x = 0; glyph_x < font->glyph_width; ++glyph_x) {
			ferro_fb_set_pixel((row[glyph_x / 8] & (1 << (7 - (glyph_x % 8)))) ? foreground : background, x + glyph_x, y + glyph_y);
		}
	}

	return ferr_ok;
};

static ferro_fb_pixel_t white_pixel = {
	.red = 0xff,
	.green = 0xff,
	.blue = 0xff,
};
static ferro_fb_pixel_t black_pixel = {
	.red = 0,
	.green = 0,
	.blue = 0,
};

static ferro_fb_coords_t next_location = {
	.x = 0,
	.y = 0,
};

#ifndef FCONSOLE_CHARACTER_PADDING_DEFAULT
	#define FCONSOLE_CHARACTER_PADDING_DEFAULT 0
#endif
#ifndef FCONSOLE_LINE_PADDING_DEFAULT
	#define FCONSOLE_LINE_PADDING_DEFAULT 0
#endif
static size_t character_padding = FCONSOLE_CHARACTER_PADDING_DEFAULT;
static size_t line_padding = FCONSOLE_LINE_PADDING_DEFAULT;

void fconsole_init() {
	fconsole_log("ferro kernel version 0.0.0 starting...\n");

	const char* orig = "foo!\n";
	char* copied = NULL;

	if (fmempool_allocate(strlen(orig) + 1, NULL, (void*)&copied) != ferr_ok) {
		fpanic();
	}

	memcpy(copied, orig, strlen(orig) + 1);

	fconsole_log(copied);

	if (fmempool_free(copied) != ferr_ok) {
		fpanic();
	}
};

ferr_t fconsole_logn(const char* string, size_t size) {
	const ferro_fb_info_t* fb_info = ferro_fb_get_info();

	while (size > 0) {
		size_t utf8_length = 0;
		uint32_t code_point = utf8_to_utf32(string, size, &utf8_length);
		bool print_it = true;

		if (code_point == UINT32_MAX) {
			goto err_out;
		}

		string += utf8_length;
		size -= utf8_length;

		if (code_point == '\n') {
			print_it = false;
		}

		if (code_point == '\n' || next_location.x + font->glyph_width >= fb_info->width) {
			next_location.x = 0;
			next_location.y += font->glyph_height + line_padding;
		}
		if (next_location.y + font->glyph_height >= fb_info->height) {
			ferro_fb_shift(true, font->glyph_height + line_padding, &black_pixel);
			next_location.y -= font->glyph_height + line_padding;
		}

		if (print_it) {
			fconsole_put_utf32_char(code_point, next_location.x, next_location.y, &white_pixel, &black_pixel);
			next_location.x += font->glyph_width + character_padding;
		}
	}

	return ferr_ok;
err_out:
	return ferr_invalid_argument;
};

ferr_t fconsole_log(const char* string) {
	return fconsole_logn(string, strlen(string));
};
