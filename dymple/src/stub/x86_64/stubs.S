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

.text

.global ___stack_chk_fail
___stack_chk_fail:
	ud2

// none of these stubs should ever be executed; they should be replaced by dymple on load

.macro stub func
	.text
	.global \func
	\func\():
		ud2
.endm

stub dyld_stub_binder
stub _dymple_load_image_by_name
stub _dymple_load_image_by_name_n
stub _dymple_load_image_from_file
stub _dymple_find_loaded_image_by_name
stub _dymple_find_loaded_image_by_name_n
stub _dymple_resolve_symbol
stub _dymple_resolve_symbol_n
stub _dymple_open_process_binary_raw

.data

.global ___stack_chk_guard
___stack_chk_guard:
	.quad 0
