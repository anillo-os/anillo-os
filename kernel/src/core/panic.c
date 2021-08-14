/**
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
//
// src/core/panic.c
//
// panic facilities for Ferro
// (for when things go downhill)
//

#include <ferro/core/panic.h>
#include <ferro/core/entry.h>
#include <ferro/core/console.h>
#include <ferro/core/interrupts.h>

void fpanicv(const char* reason_format, va_list args) {
	__builtin_debugtrap();

	// we're going to die, so don't let anyone interrupt us
	fint_disable();

	// technically, we shouldn't do this because the panic might've come from there, but oh well
	fconsole_logfv(reason_format, args);
	fconsole_log("\n");

	// for now
	fentry_hang_forever();
};

void fpanic(const char* reason_format, ...) {
	va_list args;

	va_start(args, reason_format);

	fpanicv(reason_format, args);

	// not necessary, but just for consistency
	va_end(args);
};
