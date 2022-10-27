/*
 * This file is part of Anillo OS
 * Copyright (C) 2022 Anillo OS Developers
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

#ifndef _FERRO_DRIVERS_USB_H_
#define _FERRO_DRIVERS_USB_H_

#include <ferro/base.h>
#include <ferro/error.h>

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

FERRO_DECLARATIONS_BEGIN;

FERRO_STRUCT_FWD(fusb_device);
FERRO_STRUCT_FWD(fusb_interface);

void fusb_init(void);

FERRO_WUR ferr_t fusb_device_retain(fusb_device_t* device);
void fusb_device_release(fusb_device_t* device);

/**
 * @note There may be multiple USB devices with the same vendor ID and product ID; this will only return one of them (with no guarantee as to which one).
 */
FERRO_WUR ferr_t fusb_device_lookup(uint16_t vendor_id, uint16_t product_id, fusb_device_t** out_device);

FERRO_DECLARATIONS_END;

#endif // _FERRO_DRIVERS_USB_H_
