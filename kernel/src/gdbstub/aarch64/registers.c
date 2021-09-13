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

#include <ferro/gdbstub/registers.h>

ferr_t fgdb_registers_serialize_many(fgdb_packet_buffer_t* packet_buffer, fthread_t* thread) {
	return ferr_unknown;
};

ferr_t fgdb_registers_serialize_one(fgdb_packet_buffer_t* packet_buffer, fthread_t* thread, uintmax_t id) {
	return ferr_no_such_resource;
};

ferr_t fgdb_registers_deserialize_many(fgdb_packet_buffer_t* packet_buffer, fthread_t* thread) {
	return ferr_unknown;
};

ferr_t fgdb_registers_deserialize_one(fgdb_packet_buffer_t* packet_buffer, fthread_t* thread, uintmax_t id) {
	return ferr_no_such_resource;
};

void fgdb_registers_set_single_step(fthread_t* thread) {

};

void fgdb_registers_clear_single_step(fthread_t* thread) {

};

void fgdb_registers_skip_breakpoint(void) {

};

ferr_t fgdb_registers_serialize_features(fgdb_packet_buffer_t* packet_buffer, const char* name, size_t name_length, size_t offset, size_t length) {
	return ferr_unknown;
};

ferr_t fgdb_registers_watchpoint_set(void* address, size_t size, fgdb_registers_watchpoint_type_t type) {
	return ferr_temporary_outage;
};

ferr_t fgdb_registers_watchpoint_clear(void* address) {
	return ferr_temporary_outage;
};
