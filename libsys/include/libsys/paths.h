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

#ifndef _LIBSYS_PATHS_H_
#define _LIBSYS_PATHS_H_

#include <stddef.h>
#include <stdarg.h>
#include <stdbool.h>

#include <libsys/base.h>
#include <ferro/error.h>

LIBSYS_DECLARATIONS_BEGIN;

LIBSYS_STRUCT(sys_path) {
	size_t length;
	const char* contents;
};

FERRO_STRUCT(sys_path_component) {
	size_t length;
	const char* component;

	size_t entire_path_length;
	const char* entire_path;
};

LIBSYS_WUR ferr_t sys_path_component_first_n(const char* path, size_t path_length, sys_path_component_t* out_component);
LIBSYS_WUR ferr_t sys_path_component_first(const char* path, sys_path_component_t* out_component);
LIBSYS_WUR ferr_t sys_path_component_next(sys_path_component_t* in_out_component);

LIBSYS_WUR ferr_t sys_path_join(char* out_buffer, size_t buffer_size, size_t* out_required_buffer_size, ...);
LIBSYS_WUR ferr_t sys_path_join_v(char* out_buffer, size_t buffer_size, size_t* out_required_buffer_size, va_list layers);
LIBSYS_WUR ferr_t sys_path_join_n(char* out_buffer, size_t buffer_size, size_t* out_required_buffer_size, ...);
LIBSYS_WUR ferr_t sys_path_join_nv(char* out_buffer, size_t buffer_size, size_t* out_required_buffer_size, va_list layers);
LIBSYS_WUR ferr_t sys_path_join_a(const char** layers, size_t layer_count, char* out_buffer, size_t buffer_size, size_t* out_required_buffer_size);
LIBSYS_WUR ferr_t sys_path_join_na(const char** layers, const size_t* layer_lengths, size_t layer_count, char* out_buffer, size_t buffer_size, size_t* out_required_buffer_size);
LIBSYS_WUR ferr_t sys_path_join_s(const sys_path_t* layers, size_t layer_count, char* out_buffer, size_t buffer_size, size_t* out_required_buffer_size);

LIBSYS_WUR ferr_t sys_path_join_allocate(char** out_buffer, size_t* out_buffer_size, ...);
LIBSYS_WUR ferr_t sys_path_join_allocate_v(char** out_buffer, size_t* out_buffer_size, va_list layers);
LIBSYS_WUR ferr_t sys_path_join_allocate_n(char** out_buffer, size_t* out_buffer_size, ...);
LIBSYS_WUR ferr_t sys_path_join_allocate_nv(char** out_buffer, size_t* out_buffer_size, va_list layers);
LIBSYS_WUR ferr_t sys_path_join_allocate_a(const char** layers, size_t layer_count, char** out_buffer, size_t* out_buffer_size);
LIBSYS_WUR ferr_t sys_path_join_allocate_na(const char** layers, const size_t* layer_lengths, size_t layer_count, char** out_buffer, size_t* out_buffer_size);
LIBSYS_WUR ferr_t sys_path_join_allocate_s(const sys_path_t* layers, size_t layer_count, char** out_buffer, size_t* out_buffer_size);

LIBSYS_WUR ferr_t sys_path_normalize(const char* path, char* out_buffer, size_t buffer_size, size_t* out_required_buffer_size);
LIBSYS_WUR ferr_t sys_path_normalize_n(const char* path, size_t path_length, char* out_buffer, size_t buffer_size, size_t* out_required_buffer_size);
LIBSYS_WUR ferr_t sys_path_normalize_s(const sys_path_t* path, char* out_buffer, size_t buffer_size, size_t* out_required_buffer_size);

LIBSYS_WUR ferr_t sys_path_normalize_allocate(const char* path, char** out_buffer, size_t* out_buffer_size);
LIBSYS_WUR ferr_t sys_path_normalize_allocate_n(const char* path, size_t path_length, char** out_buffer, size_t* out_buffer_size);
LIBSYS_WUR ferr_t sys_path_normalize_allocate_s(const sys_path_t* path, char** out_buffer, size_t* out_buffer_size);

LIBSYS_WUR ferr_t sys_path_file_name(const char* path, bool skip_dot, const char** out_start, size_t* out_length);
LIBSYS_WUR ferr_t sys_path_file_name_n(const char* path, size_t path_length, bool skip_dot, const char** out_start, size_t* out_length);
LIBSYS_WUR ferr_t sys_path_file_name_s(const sys_path_t* path, bool skip_dot, const char** out_start, size_t* out_length);

LIBSYS_WUR ferr_t sys_path_extension_name(const char* path, bool skip_dot, bool only_final, const char** out_start, size_t* out_length);
LIBSYS_WUR ferr_t sys_path_extension_name_n(const char* path, size_t path_length, bool skip_dot, bool only_final, const char** out_start, size_t* out_length);
LIBSYS_WUR ferr_t sys_path_extension_name_s(const sys_path_t* path, bool skip_dot, bool only_final, const char** out_start, size_t* out_length);

bool sys_path_is_absolute(const char* path);
bool sys_path_is_absolute_n(const char* path, size_t path_length);
bool sys_path_is_absolute_s(const sys_path_t* path);

LIBSYS_DECLARATIONS_END;

#endif // _LIBSYS_PATHS_H_
