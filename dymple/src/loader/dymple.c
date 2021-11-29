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

#include <libsys/libsys.private.h>

#include <dymple/util.h>
#include <dymple/images.private.h>
#include <dymple/resolution.private.h>
#include <dymple/log.h>

// FIXME: we should not be special-casing library paths
#define LIBSYS_PATH "/sys/lib/libsys.dylib"

#define SYS_HANDOFF_DESTINATION_SYMBOL_NAME "_sys_handoff_destination"

void start(void) __asm__("start");

void start(void) {
	dymple_image_t* main_image = NULL;
	dymple_image_t* libsys_image = NULL;

	sys_abort_status(sys_init());

	dymple_log_debug(dymple_log_category_general, "Hello from dymple!\n");

	dymple_abort_status(dymple_images_init(&main_image));

	// perform libsys handoff, if necessary
	if (dymple_find_loaded_image_by_name_n(LIBSYS_PATH, sizeof(LIBSYS_PATH) - 1, &libsys_image) == ferr_ok) {
		sys_handoff_context_t handoff_context;
		__typeof__(sys_handoff_destination)* sys_handoff_destination_func = NULL;

		dymple_log_debug(dymple_log_category_general, "Going to perform libsys handoff; looking up necessary symbols...\n");

		if (dymple_resolve_symbol(libsys_image, SYS_HANDOFF_DESTINATION_SYMBOL_NAME, false, (void*)&sys_handoff_destination_func) != ferr_ok) {
			dymple_log_error(dymple_log_category_general, "Failed to find libsys handoff destination function symbol\n");
			sys_abort();
		}

		dymple_log_debug(dymple_log_category_general, "Found handoff destination function at %p; begginning handoff...\n", sys_handoff_destination_func);

		if (sys_handoff_source(&handoff_context) != ferr_ok) {
			dymple_log_error(dymple_log_category_general, "Failed to start libsys handoff\n");
			sys_abort();
		}

		dymple_log_debug(dymple_log_category_general, "Source handoff complete; performing destination handoff...\n");

		if (sys_handoff_destination_func(&handoff_context) != ferr_ok) {
			dymple_log_error(dymple_log_category_general, "Failed to finish libsys handoff\n");
			sys_abort();
		}

		dymple_log_debug(dymple_log_category_general, "Handoff completed successfully\n");
	}

	// TODO: run initializers/constructors

	((dymple_entry_point_f)main_image->entry_address)();

	sys_exit(0);
};
