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

#include <con.server.h>

ferr_t conman_log_string_impl(void* _context, sys_data_t* contents, int32_t* out_status) {
	ferr_t status = ferr_ok;

	// for now
	status = sys_kernel_log_n(sys_data_contents(contents), sys_data_length(contents));

out:
	*out_status = status;
	return ferr_ok;
};
