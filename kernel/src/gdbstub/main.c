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

/**
 * @file
 *
 * GDB stub main code.
 *
 * @note This file must avoid depending on other subsystems as much as possible, to be able to debug as many other subsystems as possible.
 *       This is because we cannot safely debug subsystems that the debugger depends, as they might end up recursing back into the stub.
 */

#include <ferro/gdbstub/gdbstub.h>
#include <ferro/core/panic.h>
#include <ferro/core/console.h>
#include <ferro/core/entry.h>
#include <ferro/core/interrupts.h>
#include <ferro/gdbstub/packet-buffer.h>
#include <ferro/gdbstub/packet-buffer.private.h>
#include <ferro/core/locks.h>
#include <ferro/gdbstub/registers.h>
#include <ferro/core/paging.h>
#include <ferro/core/scheduler.private.h>
#include <ferro/core/mempool.h>

#include <libsimple/libsimple.h>

#include <stdatomic.h>

#ifndef LOG_PACKETS
	#define LOG_PACKETS 0
#endif

#define THREAD_INFO_INCLUDE_SUSPENDED 1

#define STATIC_PACKET_BUFFER_SIZE 512

#define QSUPPORTED_COMMAND "qSupported"
#define QSUPPORTED_REPLY "vContSupported+;qXfer:features:read+"

#define VCONT_QMARK_COMMAND "vCont?"
#define VCONT_QMARK_REPLY "vCont;c;C;s;S;t"

static fserial_t* fgdb_serial_port = NULL;

// this is safe because we should only ever have a packet buffer for each type of operation (one for send and one for receive)
static uint8_t fgdb_static_packet_buffer_receive[STATIC_PACKET_BUFFER_SIZE];
static uint8_t fgdb_static_packet_buffer_send[STATIC_PACKET_BUFFER_SIZE];

static atomic_bool reading_data = false;

static fint_special_handler_f breakpoint_passthrough_handler = NULL;
static fint_special_handler_f single_step_passthrough_handler = NULL;
static fint_special_handler_f watchpoint_passthrough_handler = NULL;

static void fgdb_packet_buffer_log(const fgdb_packet_buffer_t* packet_buffer);

static uint8_t fgdb_read_u8() {
	uint8_t byte = 0;

	reading_data = true;
	while (fserial_read(fgdb_serial_port, false, &byte) != ferr_ok) {
		// XXX: this should not be here! it's an internal lock subsystem function!
		farch_lock_spin_yield();
	}
	reading_data = false;

	return byte;
};

static void fgdb_write_u8(uint8_t byte) {
	while (fserial_write(fgdb_serial_port, false, byte) != ferr_ok) {
		farch_lock_spin_yield();
	}
};

static uint8_t hex_digit_value(char digit) {
	if (digit >= '0' && digit <= '9') {
		return digit - '0';
	} else if (digit >= 'a' && digit <= 'f') {
		return (digit - 'a') + 10;
	} else if (digit >= 'A' && digit <= 'F') {
		return (digit - 'A') + 10;
	} else {
		return UINT8_MAX;
	}
};

static char to_hex_digit(uint8_t value) {
	if (value < 10) {
		return value + '0';
	} else if (value < 0x10) {
		return (value - 10) + 'a';
	} else {
		return '\0';
	}
};

static ferr_t fgdb_read_ack(void) {
	uint8_t byte = fgdb_read_u8();

	if (byte == '+') {
		return ferr_ok;
	} else if (byte == '-') {
		return ferr_should_restart;
	} else {
		return ferr_unknown;
	}
};

static void fgdb_write_ack(ferr_t status) {
	if (status == ferr_ok) {
		fgdb_write_u8('+');
	} else {
		// anything else indicates we should ask the peer to resend the packet
		// (but really, @p status SHOULD be `ferr_should_restart`)
		fgdb_write_u8('-');
	}
};

static ferr_t fgdb_read_packet_start(uint8_t* in_out_running_checksum) {
	if (!in_out_running_checksum) {
		return ferr_invalid_argument;
	}

	if (fgdb_read_u8() != '$') {
		return ferr_unknown;
	}

	*in_out_running_checksum = 0;
	return ferr_ok;
};

/**
 * fgdb_read_packet_start() MUST be called for each packet prior to calling this function.
 *
 * @param                          buffer The buffer to write to.
 * @param[in,out]      in_out_buffer_size On input, the size of the buffer. On output, the number of bytes read into the buffer.
 * @param[in,out] in_out_running_checksum A pointer to a variable used to keep track of the checksum across invocations of this function for the same packet.
 *                                        The caller need not worry about its contents. This should be the same pointer given to fgdb_read_packet_start() for this packet.
 *
 * @note This function will automatically send an ACK if the checksum is valid.
 *       However, if the checksum is invalid, it is the caller's responsibility to send a NACK.
 *
 * @retval ferr_ok               The packet has been completely read and the checksum was OK.
 * @retval ferr_too_big          Part of the packet has been read into the buffer. This function should be called again to read more.
 * @retval ferr_invalid_checksum The packet has been completely read but the checksum failed.
 */
static ferr_t fgdb_read_packet(uint8_t* buffer, size_t* in_out_buffer_size, uint8_t* in_out_running_checksum) {
	uint8_t byte;
	uint8_t checksum_tmp = 0;
	uint8_t checksum_byte = 0;
	size_t bytes_read = 0;

	if (!in_out_running_checksum || !in_out_buffer_size) {
		return ferr_invalid_argument;
	}

	byte = fgdb_read_u8();
	while (byte != '#') {
		if (bytes_read == *in_out_buffer_size) {
			return ferr_too_big;
		}

		*buffer = byte;
		*in_out_running_checksum += byte;
		byte = fgdb_read_u8();

		++buffer;
		++bytes_read;
	}

	*in_out_buffer_size = bytes_read;

	byte = fgdb_read_u8();
	checksum_tmp = hex_digit_value(byte);
	if (checksum_tmp == UINT8_MAX) {
		return ferr_invalid_checksum;
	}
	checksum_byte = checksum_tmp;

	byte = fgdb_read_u8();
	checksum_tmp = hex_digit_value(byte);
	if (checksum_tmp == UINT8_MAX) {
		return ferr_invalid_checksum;
	}
	checksum_byte = (checksum_byte << 4) | checksum_tmp;

	if (*in_out_running_checksum != checksum_byte) {
		return ferr_invalid_checksum;
	}

	fgdb_write_ack(ferr_ok);

	return ferr_ok;
};

/**
 * Like a combination of fgdb_read_packet_start() and fgdb_read_packet(), but uses a packet buffer and automatically grows it as necessary.
 */
static ferr_t fgdb_read_packet_buffer_norestart(fgdb_packet_buffer_t* packet_buffer) {
	ferr_t status = ferr_ok;
	uint8_t running_checksum;

	status = fgdb_read_packet_start(&running_checksum);
	if (status != ferr_ok) {
		return status;
	}

	while (true) {
		size_t length = packet_buffer->size - packet_buffer->length;

		switch (fgdb_read_packet(&packet_buffer->buffer[packet_buffer->length], &length, &running_checksum)) {
			case ferr_ok: {
				packet_buffer->length += length;
				return ferr_ok;
			} break;

			case ferr_too_big: {
				packet_buffer->length += length;
				status = fgdb_packet_buffer_grow(packet_buffer);
				if (status != ferr_ok) {
					return status;
				}
			} break;

			case ferr_invalid_checksum: {
				return ferr_invalid_checksum;
			} break;
		}
	}
};

/**
 * Like fgdb_read_packet_buffer_norestart(), but automatically requests a packet re-send (with a NACK) if the checksum fails.
 * Thus, this function will only ever fail if there's not enough memory to expand the packet buffer.
 */
static ferr_t fgdb_read_packet_buffer(fgdb_packet_buffer_t* packet_buffer) {
	ferr_t status = ferr_ok;

retry:
	status = fgdb_read_packet_buffer_norestart(packet_buffer);

	if (status == ferr_invalid_checksum) {
		fgdb_write_ack(ferr_should_restart);
		goto retry;
	}

#if LOG_PACKETS
	fconsole_log("<- ");
	fgdb_packet_buffer_log(packet_buffer);
#endif

	return status;
};

static ferr_t fgdb_write_packet_start(uint8_t* in_out_running_checksum) {
	if (!in_out_running_checksum) {
		return ferr_invalid_argument;
	}

	fgdb_write_u8('$');

	*in_out_running_checksum = 0;
	return ferr_ok;
};

static ferr_t fgdb_write_packet(const uint8_t* buffer, size_t buffer_size, uint8_t* in_out_running_checksum) {
	if (!in_out_running_checksum) {
		return ferr_invalid_argument;
	}

	while (buffer_size > 0) {
		fgdb_write_u8(*buffer);
		*in_out_running_checksum += *buffer;

		++buffer;
		--buffer_size;
	}

	return ferr_ok;
};

/**
 * @retval ferr_ok               The packet was transmitted successfully and acknowledge with an ACK.
 * @retval ferr_invalid_argument @p in_out_running_checksum was `NULL`.
 * @retval ferr_should_restart   The peer indicated that the package was transmitted/received incorrectly (using a NACK) and should be re-sent.
 */
static ferr_t fgdb_write_packet_end(const uint8_t* buffer, size_t buffer_size, uint8_t* in_out_running_checksum) {
	ferr_t status = ferr_ok;

	status = fgdb_write_packet(buffer, buffer_size, in_out_running_checksum);
	if (status != ferr_ok) {
		return status;
	}

	fgdb_write_u8('#');

	fgdb_write_u8(to_hex_digit(*in_out_running_checksum >> 4));
	fgdb_write_u8(to_hex_digit(*in_out_running_checksum & 0x0f));

	//fconsole_logf("wrote packet; current interrupt disable count: %zu; waiting for ACK\n", FARCH_PER_CPU(outstanding_interrupt_disable_count));

	status = fgdb_read_ack();

	return status;
};

static ferr_t fgdb_write_packet_buffer_norestart(const fgdb_packet_buffer_t* packet_buffer) {
	uint8_t running_checksum = 0;
	ferr_t status = ferr_ok;

	status = fgdb_write_packet_start(&running_checksum);
	if (status != ferr_ok) {
		return status;
	}

	if (packet_buffer) {
		status = fgdb_write_packet(&packet_buffer->buffer[0], packet_buffer->length, &running_checksum);
		if (status != ferr_ok) {
			return status;
		}
	}

	status = fgdb_write_packet_end(NULL, 0, &running_checksum);
	if (status != ferr_ok) {
		return status;
	}

	return status;
};

static ferr_t fgdb_write_packet_buffer(const fgdb_packet_buffer_t* packet_buffer) {
	ferr_t status = ferr_ok;

retry:
	status = fgdb_write_packet_buffer_norestart(packet_buffer);

	if (status == ferr_should_restart) {
		goto retry;
	}

#if LOG_PACKETS
	fconsole_log("-> ");
	fgdb_packet_buffer_log(packet_buffer);
#endif

	return status;
};

static ferr_t fgdb_write_packet_empty(void) {
	return fgdb_write_packet_buffer(NULL);
};

static void fgdb_packet_buffer_log(const fgdb_packet_buffer_t* packet_buffer) {
	for (size_t i = 0; i < packet_buffer->length; ++i) {
		fconsole_logn((const char*)&packet_buffer->buffer[i], 1);
	}

	fconsole_log("\n");
};

static volatile bool should_continue = false;
static volatile bool is_initial_breakpoint = true;
static fthread_t* volatile selected_thread = NULL;

// TODO: once we get multicore support, we need to support that too

static ferr_t deserialize_thread_id(fgdb_packet_buffer_t* packet_buffer, fthread_id_t* out_id) {
	ferr_t status = ferr_ok;
	fthread_id_t thread_id = FTHREAD_ID_INVALID;

	if (packet_buffer->length - packet_buffer->offset >= 2 && packet_buffer->buffer[packet_buffer->offset] == '-' && packet_buffer->buffer[packet_buffer->offset + 1] == '1') {
		packet_buffer->offset += 2;
		thread_id = FTHREAD_ID_INVALID;
	} else {
		const char* one_past_end = NULL;
		status = simple_string_to_integer_unsigned((const char*)&packet_buffer->buffer[packet_buffer->offset], packet_buffer->length - packet_buffer->offset, &one_past_end, 0x10, (uintmax_t*)&thread_id);
		if (status == ferr_ok) {
			packet_buffer->offset = one_past_end - (const char*)packet_buffer->buffer;
			--thread_id;
		}
	}

	if (status == ferr_ok && out_id) {
		*out_id = thread_id;
	}
	return status;
};

FERRO_STRUCT(foreach_thread_serialize_id_data) {
	fgdb_packet_buffer_t* send_packet_buffer;
	bool is_first;
};

static bool foreach_thread_serialize_id(void* data, fthread_t* thread) {
	foreach_thread_serialize_id_data_t* iter_data = data;

	if (iter_data->is_first) {
		iter_data->is_first = false;
	} else {
		if (fgdb_packet_buffer_append(iter_data->send_packet_buffer, (const uint8_t*)",", sizeof(",") - 1) != ferr_ok) {
			fpanic("Failed to append to send packet");
		}
	}

	if (fgdb_packet_buffer_serialize_u64(iter_data->send_packet_buffer, thread->id + 1, true) != ferr_ok) {
		fpanic("Failed to serialize thread ID to send packet");
	}

	return true;
};

static bool foreach_thread_set_single_step(void* data, fthread_t* thread) {
	fgdb_registers_set_single_step(thread);

	return true;
};

static bool foreach_thread_clear_single_step(void* data, fthread_t* thread) {
	fgdb_registers_clear_single_step(thread);

	return true;
};

static void fgdb_serial_read_notify(void* data) {
	if (!data) {
		selected_thread = fthread_current();
	}

	fsched_foreach_thread(foreach_thread_clear_single_step, NULL, false);

	do {
		fgdb_packet_buffer_t recv_packet_buffer;

		const char* recv_data;
		const char* recv_end;
		size_t recv_length;
		bool handled = false;

		// if we're already reading data with fgdb_read_u8(), this new data belongs to it
		if (reading_data) {
			return;
		}
		// otherwise, we weren't already reading data, so this is a true asynchronous notification

		// if we're not in an interrupt context, we can't process any packets
		if (!fint_is_interrupt_context()) {
			return;
		}

		fgdb_packet_buffer_init(&recv_packet_buffer, fgdb_static_packet_buffer_receive, sizeof(fgdb_static_packet_buffer_receive));

		if (fgdb_read_packet_buffer(&recv_packet_buffer) != ferr_ok) {
			// failed to read packet
			fgdb_packet_buffer_destroy(&recv_packet_buffer);
			return;
		}

		recv_data = (const char*)recv_packet_buffer.buffer;
		recv_length = recv_packet_buffer.length;
		recv_end = recv_data + recv_length;

		if (recv_length >= 1 && (recv_data[0] == 'g' || recv_data[0] == 'G')) {
			fgdb_packet_buffer_t send_packet_buffer;
			bool ok = true;

			fgdb_packet_buffer_init(&send_packet_buffer, fgdb_static_packet_buffer_send, sizeof(fgdb_static_packet_buffer_send));

			if (ok && recv_data[0] == 'G') {
				recv_packet_buffer.offset = 1;
				ok = fgdb_registers_deserialize_many(&recv_packet_buffer, selected_thread) == ferr_ok;
			} else if (ok) {
				ok = fgdb_registers_serialize_many(&send_packet_buffer, selected_thread) == ferr_ok;
			}

			if (ok && recv_data[0] == 'G') {
				if (fgdb_packet_buffer_append(&send_packet_buffer, (const uint8_t*)"OK", sizeof("OK") - 1) != ferr_ok) {
					fpanic("Failed to append to send packet");
				}
			}

			if (!ok) {
				if (fgdb_packet_buffer_append(&send_packet_buffer, (const uint8_t*)"E00", sizeof("E00") - 1) != ferr_ok) {
					fpanic("Failed to append to send packet");
				}
			}

			if (fgdb_write_packet_buffer(&send_packet_buffer) != ferr_ok) {
				fpanic("Failed to write packet buffer");
			}

			fgdb_packet_buffer_destroy(&send_packet_buffer);

			handled = true;
		} else if (recv_length > 1 && (recv_data[0] == 'm' || recv_data[0] == 'M')) {
			// TODO: once we have userspace, threads will have the ability to have separate memory mappings,
			//       so this command will also depend on ::selected_thread

			const char* comma = simple_strnchr(recv_data, ',', recv_length);
			const char* colon = simple_strnchr(recv_data, ':', recv_length);
			bool ok = true;
			uintmax_t address;
			uintmax_t length;
			fgdb_packet_buffer_t send_packet_buffer;
			// this includes the initial 'm'/'M'
			size_t address_length;
			size_t length_length;
			size_t data_length;

			fgdb_packet_buffer_init(&send_packet_buffer, fgdb_static_packet_buffer_send, sizeof(fgdb_static_packet_buffer_send));

			if (ok && !comma) {
				ok = false;
			}

			if (ok) {
				address_length = comma - recv_data - 1;
				length_length = (colon) ? (colon - comma - 1) : (recv_end - comma - 1);

				if (colon) {
					data_length = recv_end - colon - 1;
				}
			}

			if (ok && colon && (data_length % 2) != 0) {
				ok = false;
			}

			if (ok && simple_string_to_integer_unsigned(recv_data + 1, address_length, NULL, 0x10, &address) != ferr_ok) {
				ok = false;
			}

			if (ok && simple_string_to_integer_unsigned(comma + 1, length_length, NULL, 0x10, &length) != ferr_ok) {
				ok = false;
			}

			if (ok) {
				for (size_t i = 0; i < length; ++i) {
					if (fpage_virtual_to_physical(address + i) == UINTPTR_MAX) {
						ok = false;
						break;
					}
				}
			}

			if (ok && colon) {
				// colon means we're writing
				for (size_t i = 0; i < length; ++i) {
					uintmax_t value = 0;
					if (simple_string_to_integer_unsigned(colon + 1 + (i * 2), 2, NULL, 0x10, &value) != ferr_ok) {
						ok = false;
						break;
					}
					if (value > 0xff) {
						ok = false;
						break;
					}
					((uint8_t*)address)[i] = (uint8_t)value;
				}
			} else if (ok) {
				// no colon means we're reading
				for (size_t i = 0; i < length; ++i) {
					fpanic_status(fgdb_packet_buffer_serialize_u8(&send_packet_buffer, ((const uint8_t*)address)[i], false));
				}
			}

			if (ok && colon) {
				fpanic_status(fgdb_packet_buffer_append(&send_packet_buffer, (const uint8_t*)"OK", 2));
			} else if (!ok) {
				fpanic_status(fgdb_packet_buffer_append(&send_packet_buffer, (const uint8_t*)"E00", 3));
			}

			if (fgdb_write_packet_buffer(&send_packet_buffer) != ferr_ok) {
				fpanic("Failed to write packet buffer");
			}

			fgdb_packet_buffer_destroy(&send_packet_buffer);

			handled = true;
		} else if (recv_length == sizeof(VCONT_QMARK_COMMAND) - 1 && simple_strncmp(recv_data, VCONT_QMARK_COMMAND, sizeof(VCONT_QMARK_COMMAND) - 1) == 0) {
			fgdb_packet_buffer_t send_packet_buffer;

			fgdb_packet_buffer_init(&send_packet_buffer, fgdb_static_packet_buffer_send, sizeof(fgdb_static_packet_buffer_send));

			if (fgdb_packet_buffer_append(&send_packet_buffer, (const uint8_t*)VCONT_QMARK_REPLY, sizeof(VCONT_QMARK_REPLY) - 1) != ferr_ok) {
				fpanic("Failed to append to send packet");
			}

			if (fgdb_write_packet_buffer(&send_packet_buffer) != ferr_ok) {
				fpanic("Failed to write packet buffer");
			}

			fgdb_packet_buffer_destroy(&send_packet_buffer);

			handled = true;
		} else if (recv_length > sizeof("vCont;") - 1 && simple_strncmp(recv_data, "vCont;", sizeof("vCont;") - 1) == 0) {
			const char* command_start = recv_data + (sizeof("vCont;") - 1);
			const char* semicolon = simple_strnchr(command_start, ';', recv_end - command_start);
			size_t command_length = (semicolon) ? (semicolon - command_start) : (recv_end - command_start);
			const char* command_end = command_start + command_length;

			while (command_start) {
				const char* colon = simple_strnchr(command_start, ':', command_length);
				fthread_id_t thread_id = FTHREAD_ID_INVALID;

				if (colon) {
					recv_packet_buffer.offset = (colon + 1) - recv_data;
					if (deserialize_thread_id(&recv_packet_buffer, &thread_id) != ferr_ok) {
						fpanic("Failed to parse thread ID!");
					}
					command_length = colon - command_start;
					command_end = command_start + command_length;
				}

				// TODO: actually use the thread ID given

				// TODO: the way this is supposed to work is that when a command is listed
				//       for a specific thread (or set of threads), further commands should not apply to it.
				//       (e.g. 's:1234;c' means 'step thread 1234 and continue all others')

				if (simple_strncmp(command_start, "c", command_length) == 0 || (command_length == 3 && command_start[0] == 'C')) {
					should_continue = true;
				} else if (simple_strncmp(command_start, "s", command_length) == 0 || (command_length == 3 && command_start[0] == 'S')) {
					should_continue = true;

					if (thread_id != FTHREAD_ID_INVALID) {
						fthread_t* thread = fsched_find(thread_id, false);

						fgdb_registers_set_single_step(thread);
					} else if (fthread_current() == NULL) {
						fgdb_registers_set_single_step(NULL);
					} else {
						fsched_foreach_thread(foreach_thread_set_single_step, NULL, false);
					}
				} else if (simple_strncmp(command_start, "t", command_length) == 0) {
					should_continue = false;
				}

				command_start = (semicolon) ? (semicolon + 1) : NULL;
				if (command_start) {
					semicolon = simple_strnchr(command_start, ';', recv_end - command_start);
					command_length = (semicolon) ? (semicolon - command_start) : (recv_end - command_start);
					command_end = command_start + command_length;
				}

				// TODO: support what I explained above; for now, this should work. the first command should be the most important.
				break;
			}

			handled = true;
		} else if (recv_length == sizeof("qfThreadInfo") - 1 && simple_strncmp(recv_data, "qfThreadInfo", sizeof("qfThreadInfo") - 1) == 0) {
			fgdb_packet_buffer_t send_packet_buffer;

			fgdb_packet_buffer_init(&send_packet_buffer, fgdb_static_packet_buffer_send, sizeof(fgdb_static_packet_buffer_send));

			if (!fthread_current()) {
				if (fgdb_packet_buffer_append(&send_packet_buffer, (const uint8_t*)"m1", sizeof("m1") - 1) != ferr_ok) {
					fpanic("Failed to append to send packet");
				}
			} else {
				foreach_thread_serialize_id_data_t iter_data = {
					.send_packet_buffer = &send_packet_buffer,
					.is_first = true,
				};

				if (fgdb_packet_buffer_append(&send_packet_buffer, (const uint8_t*)"m", sizeof("m") - 1) != ferr_ok) {
					fpanic("Failed to append to send packet");
				}

				fsched_foreach_thread(foreach_thread_serialize_id, &iter_data, THREAD_INFO_INCLUDE_SUSPENDED);
			}

			if (fgdb_write_packet_buffer(&send_packet_buffer) != ferr_ok) {
				fpanic("Failed to write packet buffer");
			}

			fgdb_packet_buffer_destroy(&send_packet_buffer);

			handled = true;
		} else if (recv_length == sizeof("qsThreadInfo") - 1 && simple_strncmp(recv_data, "qsThreadInfo", sizeof("qsThreadInfo") - 1) == 0) {
			fgdb_packet_buffer_t send_packet_buffer;

			fgdb_packet_buffer_init(&send_packet_buffer, fgdb_static_packet_buffer_send, sizeof(fgdb_static_packet_buffer_send));

			if (fgdb_packet_buffer_append(&send_packet_buffer, (const uint8_t*)"l", sizeof("l") - 1) != ferr_ok) {
				fpanic("Failed to append to send packet");
			}

			if (fgdb_write_packet_buffer(&send_packet_buffer) != ferr_ok) {
				fpanic("Failed to write packet buffer");
			}

			fgdb_packet_buffer_destroy(&send_packet_buffer);

			handled = true;
		} else if (recv_length == sizeof("qOffsets") - 1 && simple_strncmp(recv_data, "qOffsets", sizeof("qOffsets") - 1) == 0) {
			fgdb_packet_buffer_t send_packet_buffer;

			fgdb_packet_buffer_init(&send_packet_buffer, fgdb_static_packet_buffer_send, sizeof(fgdb_static_packet_buffer_send));

#if 0
			if (fgdb_packet_buffer_append(&send_packet_buffer, (const uint8_t*)"Text=0;Data=0;Bss=0", sizeof("Text=0;Data=0;Bss=0") - 1) != ferr_ok) {
				fpanic("Failed to append to send packet");
			}
#endif

			if (fgdb_write_packet_buffer(&send_packet_buffer) != ferr_ok) {
				fpanic("Failed to write packet buffer");
			}

			fgdb_packet_buffer_destroy(&send_packet_buffer);

			handled = true;
		} else if (recv_length == sizeof("qC") - 1 && simple_strncmp(recv_data, "qC", sizeof("qC") - 1) == 0) {
			fgdb_packet_buffer_t send_packet_buffer;
			fthread_id_t id = selected_thread ? selected_thread->id : 0;

			fgdb_packet_buffer_init(&send_packet_buffer, fgdb_static_packet_buffer_send, sizeof(fgdb_static_packet_buffer_send));

			if (fgdb_packet_buffer_append(&send_packet_buffer, (const uint8_t*)"QC", sizeof("QC") - 1) != ferr_ok) {
				fpanic("Failed to append to send packet");
			}

			if (fgdb_packet_buffer_serialize_u64(&send_packet_buffer, id + 1, true) != ferr_ok) {
				fpanic("Failed to serialize thread ID");
			}

			if (fgdb_write_packet_buffer(&send_packet_buffer) != ferr_ok) {
				fpanic("Failed to write packet buffer");
			}

			fgdb_packet_buffer_destroy(&send_packet_buffer);

			handled = true;
		} else if (recv_length == sizeof("qAttached") - 1 && simple_strncmp(recv_data, "qAttached", sizeof("qAttached") - 1) == 0) {
			fgdb_packet_buffer_t send_packet_buffer;

			fgdb_packet_buffer_init(&send_packet_buffer, fgdb_static_packet_buffer_send, sizeof(fgdb_static_packet_buffer_send));

			if (fgdb_packet_buffer_append(&send_packet_buffer, (const uint8_t*)"1", sizeof("1") - 1) != ferr_ok) {
				fpanic("Failed to append to send packet");
			}

			if (fgdb_write_packet_buffer(&send_packet_buffer) != ferr_ok) {
				fpanic("Failed to write packet buffer");
			}

			fgdb_packet_buffer_destroy(&send_packet_buffer);

			handled = true;
		} else if (recv_length > 1 && (recv_data[0] == 'p' || recv_data[0] == 'P')) {
			uintmax_t id;
			bool ok = true;
			fgdb_packet_buffer_t send_packet_buffer;
			const char* equal_sign = simple_strnchr(recv_data + 1, '=', recv_length - 1);
			size_t id_length = (equal_sign) ? (equal_sign - (recv_data + 1)) : recv_length - 1;

			fgdb_packet_buffer_init(&send_packet_buffer, fgdb_static_packet_buffer_send, sizeof(fgdb_static_packet_buffer_send));

			if (ok && simple_string_to_integer_unsigned(recv_data + 1, id_length, NULL, 0x10, &id) != ferr_ok) {
				ok = false;
			}

			if (ok && equal_sign) {
				recv_packet_buffer.offset = id_length + 2;
				ok = fgdb_registers_deserialize_one(&recv_packet_buffer, selected_thread, id) == ferr_ok;
			} else if (ok) {
				ok = fgdb_registers_serialize_one(&send_packet_buffer, selected_thread, id) == ferr_ok;
			}

			if (ok && equal_sign) {
				if (fgdb_packet_buffer_append(&send_packet_buffer, (const uint8_t*)"OK", sizeof("OK") - 1) != ferr_ok) {
					fpanic("Failed to append to send packet");
				}
			}

			if (!ok) {
				if (fgdb_packet_buffer_append(&send_packet_buffer, (const uint8_t*)"E00", sizeof("E00") - 1) != ferr_ok) {
					fpanic("Failed to append to send packet");
				}
			}

			if (fgdb_write_packet_buffer(&send_packet_buffer) != ferr_ok) {
				fpanic("Failed to write packet buffer");
			}

			fgdb_packet_buffer_destroy(&send_packet_buffer);

			handled = true;
		} else if (recv_length == sizeof("qHostInfo") - 1 && simple_strncmp(recv_data, "qHostInfo", sizeof("qHostInfo") - 1) == 0) {
			fgdb_packet_buffer_t send_packet_buffer;

			fgdb_packet_buffer_init(&send_packet_buffer, fgdb_static_packet_buffer_send, sizeof(fgdb_static_packet_buffer_send));

			if (fgdb_packet_buffer_append(&send_packet_buffer, (const uint8_t*)"cputype:16777223;cpusubtype:3;ostype:anillo;vendor:anillo;endian:little;ptrsize:8", sizeof("cputype:16777223;cpusubtype:3;ostype:anillo;vendor:anillo;endian:little;ptrsize:8") - 1) != ferr_ok) {
				fpanic("Failed to append to send packet");
			}

			if (fgdb_write_packet_buffer(&send_packet_buffer) != ferr_ok) {
				fpanic("Failed to write packet buffer");
			}

			fgdb_packet_buffer_destroy(&send_packet_buffer);

			handled = true;
		} else if (recv_length == 1 && recv_data[0] == 'k') {
			facpi_reboot();

			handled = true;
		} else if (
			recv_length >= sizeof(QSUPPORTED_COMMAND) - 1                               &&
			simple_strncmp(recv_data, QSUPPORTED_COMMAND, sizeof(QSUPPORTED_COMMAND) - 1) == 0 &&
			(
				recv_length == sizeof(QSUPPORTED_COMMAND) - 1    ||
				recv_data[sizeof(QSUPPORTED_COMMAND) - 1] == ':'
			)
		) {
			fgdb_packet_buffer_t send_packet_buffer;

			fgdb_packet_buffer_init(&send_packet_buffer, fgdb_static_packet_buffer_send, sizeof(fgdb_static_packet_buffer_send));

			// TODO: actually parse this
			//if (data_length > sizeof("qSupported") - 1) {
			//	
			//}

			if (fgdb_packet_buffer_append(&send_packet_buffer, (const uint8_t*)QSUPPORTED_REPLY, sizeof(QSUPPORTED_REPLY) - 1) != ferr_ok) {
				fpanic("Failed to append to send packet");
			}

			if (fgdb_write_packet_buffer(&send_packet_buffer) != ferr_ok) {
				fpanic("Failed to write packet");
			}

			fgdb_packet_buffer_destroy(&send_packet_buffer);

			handled = true;
		} else if (recv_length == 1 && recv_data[0] == '?') {
			fgdb_packet_buffer_t send_packet_buffer;
			fthread_id_t id = 0;

			fgdb_packet_buffer_init(&send_packet_buffer, fgdb_static_packet_buffer_send, sizeof(fgdb_static_packet_buffer_send));

			if (fthread_current()) {
				id = fthread_current()->id;
			}

			if (fgdb_packet_buffer_append(&send_packet_buffer, (const uint8_t*)"T05thread:", sizeof("T05thread:") - 1) != ferr_ok) {
				fpanic("Failed to append to packet");
			}

			if (fgdb_packet_buffer_serialize_u64(&send_packet_buffer, id + 1, true) != ferr_ok) {
				fpanic("Failed to serialize thread ID");
			}

			if (fgdb_packet_buffer_append(&send_packet_buffer, (const uint8_t*)";", sizeof(";") - 1) != ferr_ok) {
				fpanic("Failed to append to packet");
			}

			if (fgdb_write_packet_buffer(&send_packet_buffer) != ferr_ok) {
				fpanic("Failed to write packet");
			}

			fgdb_packet_buffer_destroy(&send_packet_buffer);

			handled = true;
		} else if (recv_length > 0 && recv_data[0] == 'H') {
			fgdb_packet_buffer_t send_packet_buffer;
			fthread_id_t id;
			bool ok = true;
			fthread_t* thread = NULL;

			fgdb_packet_buffer_init(&send_packet_buffer, fgdb_static_packet_buffer_send, sizeof(fgdb_static_packet_buffer_send));

			recv_packet_buffer.offset = 2;

			if (deserialize_thread_id(&recv_packet_buffer, &id) != ferr_ok) {
				ok = false;
			}

			if (fthread_current()) {
				if (ok) {
					thread = fsched_find(id, false);
					if (!thread) {
						ok = false;
					}
				}

				if (ok) {
					selected_thread = thread;
				}
			} else if (id != 0) {
				ok = false;
			}

			if (ok) {
				if (fgdb_packet_buffer_append(&send_packet_buffer, (const uint8_t*)"OK", sizeof("OK") - 1) != ferr_ok) {
					fpanic("Failed to append to send packet");
				}
			} else {
				if (fgdb_packet_buffer_append(&send_packet_buffer, (const uint8_t*)"E00", sizeof("E00") - 1) != ferr_ok) {
					fpanic("Failed to append to send packet");
				}
			}

			if (fgdb_write_packet_buffer(&send_packet_buffer) != ferr_ok) {
				fpanic("Failed to write packet");
			}

			fgdb_packet_buffer_destroy(&send_packet_buffer);

			handled = true;
		} else if (recv_length > 0 && recv_data[0] == 'T') {
			fgdb_packet_buffer_t send_packet_buffer;
			fthread_id_t id;
			bool ok = true;
			fthread_t* thread = NULL;

			fgdb_packet_buffer_init(&send_packet_buffer, fgdb_static_packet_buffer_send, sizeof(fgdb_static_packet_buffer_send));

			recv_packet_buffer.offset = 1;

			if (deserialize_thread_id(&recv_packet_buffer, &id) != ferr_ok) {
				ok = false;
			}

			if (ok) {
				thread = fsched_find(id, false);
				if (!thread) {
					ok = false;
				}
			}

			if (ok) {
				selected_thread = thread;
			}

			if (ok) {
				if (fgdb_packet_buffer_append(&send_packet_buffer, (const uint8_t*)"OK", sizeof("OK") - 1) != ferr_ok) {
					fpanic("Failed to append to send packet");
				}
			} else {
				if (fgdb_packet_buffer_append(&send_packet_buffer, (const uint8_t*)"E00", sizeof("E00") - 1) != ferr_ok) {
					fpanic("Failed to append to send packet");
				}
			}

			if (fgdb_write_packet_buffer(&send_packet_buffer) != ferr_ok) {
				fpanic("Failed to write packet");
			}

			fgdb_packet_buffer_destroy(&send_packet_buffer);

			handled = true;
		} else if (recv_length > sizeof("qXfer:features:read:") - 1 && simple_strncmp(recv_data, "qXfer:features:read:", sizeof("qXfer:features:read:") - 1) == 0) {
			fgdb_packet_buffer_t send_packet_buffer;
			bool ok = true;
			const char* name = recv_data + (sizeof("qXfer:features:read:") - 1);
			const char* annex_colon = simple_strnchr(name, ':', recv_length - sizeof("qXfer:features:read:") - 1);
			const char* comma = annex_colon ? simple_strnchr(annex_colon + 1, ',', recv_end - (annex_colon + 1)) : NULL;
			size_t name_length = (annex_colon) ? (annex_colon - name) : 0;
			size_t offset = 0;
			size_t length = 0;

			fgdb_packet_buffer_init(&send_packet_buffer, fgdb_static_packet_buffer_send, sizeof(fgdb_static_packet_buffer_send));

			if (ok && !annex_colon) {
				ok = false;
			}

			if (ok && !comma) {
				ok = false;
			}

			if (ok) {
				ok = simple_string_to_integer_unsigned(annex_colon + 1, comma - (annex_colon + 1), NULL, 0x10, &offset) == ferr_ok;
			}

			if (ok) {
				ok = simple_string_to_integer_unsigned(comma + 1, recv_end - (comma + 1), NULL, 0x10, &length) == ferr_ok;
			}

			if (ok) {
				ok = fgdb_registers_serialize_features(&send_packet_buffer, name, name_length, offset, length) == ferr_ok;
			}

			if (!ok) {
				if (fgdb_packet_buffer_append(&send_packet_buffer, (const uint8_t*)"E00", sizeof("E00") - 1) != ferr_ok) {
					fpanic("Failed to append to send packet");
				}
			}

			if (fgdb_write_packet_buffer(&send_packet_buffer) != ferr_ok) {
				fpanic("Failed to write packet");
			}

			fgdb_packet_buffer_destroy(&send_packet_buffer);

			handled = true;
		} else if (recv_length == 1 && recv_data[0] == 'c') {
			should_continue = true;
			handled = true;
		} else if (recv_length == 1 && recv_data[0] == 's') {
			should_continue = true;
			fgdb_registers_set_single_step(selected_thread);
			handled = true;
		} else if (recv_length > 2 && recv_data[0] == '_' && recv_data[1] == 'M') {
			fgdb_packet_buffer_t send_packet_buffer;
			bool ok = true;
			size_t size = 0;
			const char* after_size = NULL;
			void* addr = NULL;

			fgdb_packet_buffer_init(&send_packet_buffer, fgdb_static_packet_buffer_send, sizeof(fgdb_static_packet_buffer_send));

			if (ok) {
				ok = simple_string_to_integer_unsigned(&recv_data[2], recv_length - 2, &after_size, 0x10, &size) == ferr_ok;
			}

			if (ok && *after_size != ',') {
				ok = false;
			}

			// TODO: parse permissions

			if (ok) {
				ok = fmempool_allocate(size, NULL, &addr) == ferr_ok;
			}

			if (ok) {
				ok = fgdb_packet_buffer_serialize_u64(&send_packet_buffer, (uintptr_t)addr, true);
			}

			if (!ok) {
				if (fgdb_packet_buffer_append(&send_packet_buffer, (const uint8_t*)"E00", sizeof("E00") - 1) != ferr_ok) {
					fpanic("Failed to append to send packet");
				}
			}

			if (fgdb_write_packet_buffer(&send_packet_buffer) != ferr_ok) {
				fpanic("Failed to write packet");
			}

			fgdb_packet_buffer_destroy(&send_packet_buffer);

			handled = true;
		} else if (recv_length > 2 && recv_data[0] == '_' && recv_data[1] == 'm') {
			fgdb_packet_buffer_t send_packet_buffer;
			bool ok = true;
			void* addr = NULL;

			fgdb_packet_buffer_init(&send_packet_buffer, fgdb_static_packet_buffer_send, sizeof(fgdb_static_packet_buffer_send));

			if (ok) {
				ok = simple_string_to_integer_unsigned(&recv_data[2], recv_length - 2, NULL, 0x10, (uintmax_t*)&addr) == ferr_ok;
			}

			if (ok) {
				ok = fmempool_free(addr) == ferr_ok;
			}

			if (ok) {
				if (fgdb_packet_buffer_append(&send_packet_buffer, (const uint8_t*)"OK", sizeof("OK") - 1) != ferr_ok) {
					fpanic("Failed to append to send packet");
				}
			} else {
				if (fgdb_packet_buffer_append(&send_packet_buffer, (const uint8_t*)"E00", sizeof("E00") - 1) != ferr_ok) {
					fpanic("Failed to append to send packet");
				}
			}

			if (fgdb_write_packet_buffer(&send_packet_buffer) != ferr_ok) {
				fpanic("Failed to write packet");
			}

			fgdb_packet_buffer_destroy(&send_packet_buffer);

			handled = true;
		} else if (recv_length > 3 && (recv_data[0] == 'z' || recv_data[0] == 'Z') && (recv_data[1] == '2' || recv_data[1] == '3' || recv_data[1] == '4')) {
			fgdb_packet_buffer_t send_packet_buffer;
			bool ok = true;
			void* addr = NULL;
			size_t size = 0;
			const char* comma = simple_strnchr(&recv_data[3], ',', recv_length - 3);

			fgdb_packet_buffer_init(&send_packet_buffer, fgdb_static_packet_buffer_send, sizeof(fgdb_static_packet_buffer_send));

			if (ok && !comma) {
				ok = false;
			}

			if (ok) {
				ok = simple_string_to_integer_unsigned(&recv_data[3], comma - &recv_data[3], NULL, 0x10, (uintmax_t*)&addr) == ferr_ok;
			}

			if (ok) {
				ok = simple_string_to_integer_unsigned(comma + 1, recv_end - (comma + 1), NULL, 0x10, &size) == ferr_ok;
			}

			if (ok) {
				if (recv_data[0] == 'Z') {
					ok = fgdb_registers_watchpoint_set(addr, size, ((recv_data[1] == '2' || recv_data[1] == '4') ? fgdb_registers_watchpoint_type_write : 0) | ((recv_data[1] == '3' || recv_data[1] == '4') ? fgdb_registers_watchpoint_type_read : 0)) == ferr_ok;
				} else {
					ok = fgdb_registers_watchpoint_clear(addr) == ferr_ok;
				}
			}

			if (ok) {
				if (fgdb_packet_buffer_append(&send_packet_buffer, (const uint8_t*)"OK", sizeof("OK") - 1) != ferr_ok) {
					fpanic("Failed to append to send packet");
				}
			} else {
				if (fgdb_packet_buffer_append(&send_packet_buffer, (const uint8_t*)"E00", sizeof("E00") - 1) != ferr_ok) {
					fpanic("Failed to append to send packet");
				}
			}

			if (fgdb_write_packet_buffer(&send_packet_buffer) != ferr_ok) {
				fpanic("Failed to write packet");
			}

			fgdb_packet_buffer_destroy(&send_packet_buffer);

			handled = true;
		}

		if (!handled) {
			fgdb_write_packet_empty();
		}

		fgdb_packet_buffer_destroy(&recv_packet_buffer);
	} while (!data && !should_continue);
};

static void fgdb_breakpoint_handler_common(void* data) {
	fthread_id_t id = 0;

	selected_thread = fthread_current();

	if (!is_initial_breakpoint) {
		fgdb_packet_buffer_t send_packet_buffer;

		fgdb_packet_buffer_init(&send_packet_buffer, fgdb_static_packet_buffer_send, sizeof(fgdb_static_packet_buffer_send));

		if (fthread_current()) {
			id = fthread_current()->id;
		}

		if (fgdb_packet_buffer_append(&send_packet_buffer, (const uint8_t*)"T05thread:", sizeof("T05thread:") - 1) != ferr_ok) {
			fpanic("Failed to append to packet");
		}

		if (fgdb_packet_buffer_serialize_u64(&send_packet_buffer, id + 1, true) != ferr_ok) {
			fpanic("Failed to serialize thread ID");
		}

		if (fgdb_packet_buffer_append(&send_packet_buffer, (const uint8_t*)";", sizeof(";") - 1) != ferr_ok) {
			fpanic("Failed to append to packet");
		}

		if (fgdb_write_packet_buffer(&send_packet_buffer) != ferr_ok) {
			fpanic("Failed to write packet");
		}

		fgdb_packet_buffer_destroy(&send_packet_buffer);
	}

	should_continue = false;

	if (is_initial_breakpoint) {
		is_initial_breakpoint = false;
		fgdb_registers_skip_breakpoint();
	}

	while (!should_continue) {
		farch_lock_spin_yield();
		fgdb_serial_read_notify((void*)1);
	}

	should_continue = false;
};

static void fgdb_breakpoint_handler(void* data) {
	fgdb_breakpoint_handler_common(data);

	// TODO: determine when it's appropriate to call the passthrough handler
#if 0
	if (breakpoint_passthrough_handler) {
		breakpoint_passthrough_handler(NULL);
	}
#endif
};

static void fgdb_single_step_handler(void* data) {
	// this actually has the exact same behavior as a breakpoint
	fgdb_breakpoint_handler_common(data);

	// TODO: determine when it's appropriate to call the passthrough handler
#if 0
	if (watchpoint_passthrough_handler) {
		watchpoint_passthrough_handler(NULL);
	}
#endif
};

static void fgdb_watchpoint_handler(void* data) {
	fthread_id_t id = 0;
	fgdb_packet_buffer_t send_packet_buffer;

	selected_thread = fthread_current();

	fgdb_packet_buffer_init(&send_packet_buffer, fgdb_static_packet_buffer_send, sizeof(fgdb_static_packet_buffer_send));

	if (fthread_current()) {
		id = fthread_current()->id;
	}

	if (fgdb_packet_buffer_append(&send_packet_buffer, (const uint8_t*)"T05thread:", sizeof("T05thread:") - 1) != ferr_ok) {
		fpanic("Failed to append to packet");
	}

	if (fgdb_packet_buffer_serialize_u64(&send_packet_buffer, id + 1, true) != ferr_ok) {
		fpanic("Failed to serialize thread ID");
	}

	if (fgdb_packet_buffer_append(&send_packet_buffer, (const uint8_t*)";", sizeof(";") - 1) != ferr_ok) {
		fpanic("Failed to append to packet");
	}

	// TODO: add info on which address triggered the watchpoint

	if (fgdb_write_packet_buffer(&send_packet_buffer) != ferr_ok) {
		fpanic("Failed to write packet");
	}

	fgdb_packet_buffer_destroy(&send_packet_buffer);

	should_continue = false;

	while (!should_continue) {
		farch_lock_spin_yield();
		fgdb_serial_read_notify((void*)1);
	}

	should_continue = false;

	// TODO: determine when it's appropriate to call the passthrough handler
#if 0
	if (watchpoint_passthrough_handler) {
		watchpoint_passthrough_handler(NULL);
	}
#endif
};

void fgdb_init(fserial_t* serial_port) {
	bool should_process_more = true;

	fgdb_serial_port = serial_port;

	while (fserial_connected(fgdb_serial_port) != ferr_ok) {
		fentry_idle();
	}

	if (fgdb_read_ack() != ferr_ok) {
		fpanic("Debug serial port did not receive initial ACK");
	}

	fint_disable();

	// register for new data notifications
	if (fserial_read_notify(serial_port, fgdb_serial_read_notify, NULL) != ferr_ok) {
		fpanic("Failed to register serial port data notification callback");
	}

	if (fint_register_special_handler(fint_special_interrupt_common_breakpoint, fgdb_breakpoint_handler, NULL) != ferr_ok) {
		fpanic("Failed to register breakpoint interrupt handler");
	}

	if (fint_register_special_handler(fint_special_interrupt_common_single_step, fgdb_single_step_handler, NULL) != ferr_ok) {
		fpanic("Failed to register single-step interrupt handler");
	}

	if (fint_register_special_handler(fint_special_interrupt_common_watchpoint, fgdb_watchpoint_handler, NULL) != ferr_ok) {
		fpanic("Failed to register watchpoint interrupt handler");
	}

	// trigger our first breakpoint to start processing packets
	__builtin_debugtrap();

	fint_enable();
};

ferr_t fgdb_register_passthrough_handlers(fint_special_handler_f breakpoint, fint_special_handler_f single_step, fint_special_handler_f watchpoint) {
	breakpoint_passthrough_handler = breakpoint;
	single_step_passthrough_handler = single_step;
	watchpoint_passthrough_handler = watchpoint;
	return ferr_ok;
};
