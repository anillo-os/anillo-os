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

#ifndef _NETMAN_DEVICE_H_
#define _NETMAN_DEVICE_H_

#include <stddef.h>
#include <stdint.h>

#include <netman/base.h>
#include <libsys/libsys.h>

NETMAN_DECLARATIONS_BEGIN;

NETMAN_STRUCT_FWD(netman_packet);
NETMAN_STRUCT_FWD(netman_device);

typedef void (*netman_device_transmit_packet_callback_f)(void* data, ferr_t status);
typedef ferr_t (*netman_device_packet_receive_hook_f)(void* data, netman_packet_t* packet);

netman_device_t* netman_device_any(void);

NETMAN_WUR ferr_t netman_device_transmit_packet(netman_device_t* device, netman_packet_t* packet, netman_device_transmit_packet_callback_f callback, void* data);

NETMAN_WUR ferr_t netman_device_register_packet_receive_hook(netman_device_t* device, netman_device_packet_receive_hook_f hook, void* data);

NETMAN_WUR ferr_t netman_device_register_global_packet_receive_hook(netman_device_packet_receive_hook_f hook, void* data);

const uint8_t* netman_device_mac_address(netman_device_t* device);

NETMAN_DECLARATIONS_END;

#endif // _NETMAN_DEVICE_H_
