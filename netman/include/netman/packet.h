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

#ifndef _NETMAN_PACKET_H_
#define _NETMAN_PACKET_H_

#include <netman/base.h>
#include <libsys/libsys.h>

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

NETMAN_DECLARATIONS_BEGIN;

NETMAN_STRUCT_FWD(netman_packet);

/**
 * Creates a new, empty packet.
 *
 * @param out_packet An out-pointer in which a pointer to the newly created packet will be written.
 *
 * @retval ferr_ok               The packet was successfully created.
 * @retval ferr_invalid_argument One or more of: 1) @p out_packet was `NULL`.
 * @retval ferr_temporary_outage There were insufficient resources to create the packet.
 */
NETMAN_WUR ferr_t netman_packet_create(netman_packet_t** out_packet);

size_t netman_packet_length(netman_packet_t* packet);

/**
 * Appends the given data to the packet.
 *
 * @param packet The packet to append the data to.
 * @param data   The start address of the data to add to the packet.
 * @param length The length of the region, in bytes.
 * @param out_copied An optional-pointer in which to write the number of bytes copied into the packet.
 *                   Note that this will be written to even in the case of failure.
 *
 * @retval ferr_ok               The data was successfully and completely appended.
 * @retval ferr_invalid_argument One or more of: 1) @p data was an invalid pointer (e.g. `NULL`), 2) @p length was invalid (`0` or too large).
 * @retval ferr_temporary_outage There were insufficient resources to complete the operation.
 *                               Note that the data may have been partially copied; you can check @p out_copied to see how much was copied (starting from the start of the buffer).
 */
NETMAN_WUR ferr_t netman_packet_append(netman_packet_t* packet, const void* data, size_t length, size_t* out_copied);

/**
 * Appends the given data to the packet without copying it.
 *
 * @param packet The packet to append the data to.
 * @param data   The start address of the data to add to the packet. This address MUST be page-aligned.
 * @param length The length of the region, in bytes. This is limited to a maximum of 8KiB (due to the way packet data is used internally).
 *
 * @note This function transfers ownership of the given memory region into the packet.
 *       The caller MUST NOT continue to use the memory.
 *
 * @note If the packet's size before appending this data is not a multiple of the system page size,
 *       the packet will be automatically extended to a multiple of the system page size.
 *       Any bytes in this expanded region will be zeroed before the new data is appended.
 *
 * @retval ferr_ok               The data was successfully appended.
 * @retval ferr_invalid_argument One or more of: 1) @p data was an invalid pointer (`NULL` or not page-aligned), 2) @p length was invalid (`0` or too large).
 * @retval ferr_temporary_outage There were insufficient resources to complete the operation.
 */
NETMAN_WUR ferr_t netman_packet_append_no_copy(netman_packet_t* packet, void* data, size_t length);

/**
 * Extends the packet by the given number of bytes, optionally zeroing out the new data.
 *
 * @param packet       The packet to extend.
 * @param length       The number of bytes to extend the packet by.
 * @param zero         Whether to zero out the new data.
 * @param out_extended An optional-pointer in which to write the number of bytes the packet was extended by.
 *                     Note that this will be written to even in the case of failure.
 *
 * @retval ferr_ok               The packet was successfully extended.
 * @retval ferr_invalid_argument One or more of: 1) @p length was invalid (`0` or too large).
 * @retval ferr_temporary_outage There were insufficient resources to complete the operation.
 *                               Note that the packet may have been partially extended; you can check @p out_extended to see how much was it was extended by.
 */
NETMAN_WUR ferr_t netman_packet_extend(netman_packet_t* packet, size_t length, bool zero, size_t* out_extended);

/**
 * Maps the data contained within the packet into memory.
 *
 * This mapping remains valid until either:
 *   * The packet length is modified (e.g. by appending data to it), OR
 *   * The packet is destroyed.
 *
 * @param packet     The packet whose data will be mapped into memory.
 * @param out_data   An out-pointer in which to write the start of the resulting mapped region.
 * @param out_length An optional out-pointer in which to write the length, in bytes, of the mapped region.
 *
 * @note No guarantees are made about the alignment of the mapped region. Furthermore, the region isn't even
 *       guaranteed to be mapped using paging functions; it may well be part of a memory pool.
 *
 * @retval ferr_ok               The mapping was successfully created.
 * @retval ferr_invalid_argument One or more of: 1) @p out_data was an invalid pointer (e.g. `NULL`).
 * @retval ferr_temporary_outage There were insufficient resources to complete the operation.
 */
NETMAN_WUR ferr_t netman_packet_map(netman_packet_t* packet, void** out_data, size_t* out_length);

void netman_packet_destroy(netman_packet_t* packet);

NETMAN_DECLARATIONS_END;;

#endif // _NETMAN_NET_PACKET_H_
