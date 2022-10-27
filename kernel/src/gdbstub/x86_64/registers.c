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
 * GDB stub register management for x86.
 */

#include <ferro/gdbstub/registers.h>
#include <ferro/gdbstub/packet-buffer.private.h>
#include <ferro/core/interrupts.h>
#include <ferro/core/panic.h>
#include <ferro/core/threads.h>

#include <gen/ferro/gdbstub/target.xml.h>

#include <libsimple/libsimple.h>

#define U128_XXX "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
#define U80_XXX "xxxxxxxxxxxxxxxxxxxx"
#define U64_XXX "xxxxxxxxxxxxxxxx"
#define U32_XXX "xxxxxxxx"

#define FOREACH_REGISTER_NORMAL(_macro_present, _macro_xxx) \
	case 0: \
		_macro_present(64, rax); \
	case 1: \
		_macro_present(64, rbx); \
	case 2: \
		_macro_present(64, rcx); \
	case 3: \
		_macro_present(64, rdx); \
	case 4: \
		_macro_present(64, rsi); \
	case 5: \
		_macro_present(64, rdi); \
	case 6: \
		_macro_present(64, rbp); \
	case 7: \
		_macro_present(64, rsp); \
	case 8: \
		_macro_present(64, r8); \
	case 9: \
		_macro_present(64, r9); \
	case 10: \
		_macro_present(64, r10); \
	case 11: \
		_macro_present(64, r11); \
	case 12: \
		_macro_present(64, r12); \
	case 13: \
		_macro_present(64, r13); \
	case 14: \
		_macro_present(64, r14); \
	case 15: \
		_macro_present(64, r15); \
	case 16: \
		_macro_present(64, rip); \
	case 17: \
		_macro_present(32, rflags); \
	case 18: \
		_macro_present(32, cs); \
	case 19: \
		_macro_present(32, ss); \
	case 20: \
		_macro_xxx(32); \
	case 21: \
		_macro_xxx(32); \
	case 22: \
		_macro_xxx(32); \
	case 23: \
		_macro_xxx(32);

#define FOREACH_REGISTER_FPU(_macro_present, _macro_xxx) \
	/* TODO: Ferro doesn't have FPU/SSE support yet, so these are irrelevant (but GDB still wants them) */ \
	case 24: \
		_macro_xxx(80); /* st0 */ \
	case 25: \
		_macro_xxx(80); /* st1 */ \
	case 26: \
		_macro_xxx(80); /* st2 */ \
	case 27: \
		_macro_xxx(80); /* st3 */ \
	case 28: \
		_macro_xxx(80); /* st4 */ \
	case 29: \
		_macro_xxx(80); /* st5 */ \
	case 30: \
		_macro_xxx(80); /* st6 */ \
	case 31: \
		_macro_xxx(80); /* st7 */

#define FOREACH_REGISTER_FPU_CONTROL(_macro_present, _macro_xxx) \
	case 32: \
		_macro_xxx(32); /* fctrl */ \
	case 33: \
		_macro_xxx(32); /* fstat */ \
	case 34: \
		_macro_xxx(32); /* ftag */ \
	case 35: \
		_macro_xxx(32); /* fiseg */ \
	case 36: \
		_macro_xxx(32); /* fioff */ \
	case 37: \
		_macro_xxx(32); /* foseg */ \
	case 38: \
		_macro_xxx(32); /* fooff */ \
	case 39: \
		_macro_xxx(32); /* fop */

#define FOREACH_REGISTER_SSE(_macro_present, _macro_xxx) \
	case 40: \
		_macro_xxx(128); /* xmm0 */ \
	case 41: \
		_macro_xxx(128); /* xmm1 */ \
	case 42: \
		_macro_xxx(128); /* xmm2 */ \
	case 43: \
		_macro_xxx(128); /* xmm3 */ \
	case 44: \
		_macro_xxx(128); /* xmm4 */ \
	case 45: \
		_macro_xxx(128); /* xmm5 */ \
	case 46: \
		_macro_xxx(128); /* xmm6 */ \
	case 47: \
		_macro_xxx(128); /* xmm7 */ \
	case 48: \
		_macro_xxx(128); /* xmm8 */ \
	case 49: \
		_macro_xxx(128); /* xmm9 */ \
	case 50: \
		_macro_xxx(128); /* xmm10 */ \
	case 51: \
		_macro_xxx(128); /* xmm11 */ \
	case 52: \
		_macro_xxx(128); /* xmm12 */ \
	case 53: \
		_macro_xxx(128); /* xmm13 */ \
	case 54: \
		_macro_xxx(128); /* xmm14 */ \
	case 55: \
		_macro_xxx(128); /* xmm15 */ \

#define FOREACH_REGISTER_SSE_CONTROL(_macro_present, _macro_xxx) \
	case 56: \
		_macro_xxx(32); /* mxcsr */ \

#define FOREACH_REGISTER_OTHER(_macro_present, _macro_xxx) \
	case 57: \
		_macro_present(64, rax); /* orig rax...? how is this different? */ \
	case 58: \
		_macro_xxx(64); /* fs_base */ \
	case 59: \
		_macro_xxx(64); /* gs_base */

static ferr_t fgdb_registers_serialize_one_with_thread(fgdb_packet_buffer_t* packet_buffer, uintmax_t id, fthread_t* thread) {
	farch_int_frame_flat_registers_union_t* flat_frame = (void*)fint_current_frame();
	bool use_frame = !thread || thread == fthread_current(); // TODO: this is wrong when we implement multicore support

	#define SERIALIZE(_bits, _name) return fgdb_packet_buffer_serialize_u ## _bits(packet_buffer, (use_frame) ? (uint ## _bits ## _t)flat_frame->flat._name : (uint ## _bits ## _t)thread->saved_context->_name, FERRO_ENDIANNESS == FERRO_ENDIANNESS_BIG)
	#define SERIALIZE_XXX(_bits) return fgdb_packet_buffer_append(packet_buffer, (const uint8_t*)U ## _bits ## _XXX, sizeof(U ## _bits ## _XXX) - 1)

	switch (id) {
		FOREACH_REGISTER_NORMAL(SERIALIZE, SERIALIZE_XXX);
		FOREACH_REGISTER_FPU(SERIALIZE, SERIALIZE_XXX);
		FOREACH_REGISTER_FPU_CONTROL(SERIALIZE, SERIALIZE_XXX);
		FOREACH_REGISTER_SSE(SERIALIZE, SERIALIZE_XXX);
		FOREACH_REGISTER_SSE_CONTROL(SERIALIZE, SERIALIZE_XXX);
		FOREACH_REGISTER_OTHER(SERIALIZE, SERIALIZE_XXX);

		default:
			return ferr_no_such_resource;
	};

	#undef SERIALIZE
	#undef SERIALIZE_XXX
};

FERRO_ALWAYS_INLINE bool is_x(char character) {
	return character == 'x' || character == 'X';
};

static bool skip_x(fgdb_packet_buffer_t* packet_buffer, size_t bits) {
	if (!is_x(packet_buffer->buffer[packet_buffer->offset])) {
		return false;
	}

	++packet_buffer->offset;

	for (size_t i = 1; i < bits / 4; ++i) {
		if (packet_buffer->offset >= packet_buffer->length) {
			break;
		}

		if (!is_x(packet_buffer->buffer[packet_buffer->offset])) {
			break;
		}

		++packet_buffer->offset;
	}

	return true;
};

static ferr_t fgdb_registers_deserialize_one_with_thread(fgdb_packet_buffer_t* packet_buffer, uintmax_t id, fthread_t* thread) {
	farch_int_frame_flat_registers_union_t* flat_frame = (void*)fint_current_frame();
	bool use_frame = !thread || thread == fthread_current(); // TODO: this is wrong when we implement multicore support

	#define DESERIALIZE_NORMAL(_bits, _name) { \
			if (packet_buffer->offset >= packet_buffer->length) { \
				return ferr_invalid_argument; \
			} \
			if (!skip_x(packet_buffer, _bits)) { \
				uint ## _bits ## _t _value = 0; \
				ferr_t _status = fgdb_packet_buffer_deserialize_u ## _bits(packet_buffer, FERRO_ENDIANNESS == FERRO_ENDIANNESS_BIG, &_value); \
				if (_status != ferr_ok) { \
					return _status; \
				} \
				if (use_frame) { \
					flat_frame->flat._name = _value; \
				} else { \
					thread->saved_context->_name = _value; \
				} \
			} \
			return ferr_ok; \
		} break;

	// just assign the result to itself
	#define DESERIALIZE_XXX_NORMAL(_bits)  { \
			if (packet_buffer->offset >= packet_buffer->length) { \
				return ferr_invalid_argument; \
			} \
			if (!skip_x(packet_buffer, _bits)) { \
				uint ## _bits ## _t _value = 0; \
				ferr_t _status = fgdb_packet_buffer_deserialize_u ## _bits(packet_buffer, FERRO_ENDIANNESS == FERRO_ENDIANNESS_BIG, &_value); \
				if (_status != ferr_ok) { \
					return _status; \
				} \
			} \
			return ferr_ok; \
		} break;

	#define DESERIALIZE_FPU(_bits, _destination) _Static_assert(0, "This should not be expanded (yet)")
	#define DESERIALIZE_XXX_FPU(_bits) \
		if (!skip_x(packet_buffer, _bits)) { \
			return ferr_invalid_argument; \
		} \
		return ferr_ok;

	#define DESERIALIZE_SSE(_bits, _destination) _Static_assert(0, "This should not be expanded (yet)")
	#define DESERIALIZE_XXX_SSE(_bits) \
		if (!skip_x(packet_buffer, _bits)) { \
			return ferr_invalid_argument; \
		} \
		return ferr_ok;

	switch (id) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wint-conversion"
		FOREACH_REGISTER_NORMAL(DESERIALIZE_NORMAL, DESERIALIZE_XXX_NORMAL);
#pragma GCC diagnostic pop
		FOREACH_REGISTER_FPU(DESERIALIZE_FPU, DESERIALIZE_XXX_FPU);
		FOREACH_REGISTER_FPU_CONTROL(DESERIALIZE_NORMAL, DESERIALIZE_XXX_NORMAL);
		FOREACH_REGISTER_SSE(DESERIALIZE_NORMAL, DESERIALIZE_XXX_SSE);
		FOREACH_REGISTER_SSE_CONTROL(DESERIALIZE_NORMAL, DESERIALIZE_XXX_NORMAL);
		FOREACH_REGISTER_OTHER(DESERIALIZE_NORMAL, DESERIALIZE_XXX_NORMAL);

		default:
			return ferr_no_such_resource;
	}

	#undef DESERIALIZE_NORMAL
	#undef DESERIALIZE_XXX_NORMAL
};

ferr_t fgdb_registers_serialize_many(fgdb_packet_buffer_t* packet_buffer, fthread_t* thread) {
	for (size_t i = 0; i < 24; ++i) {
		ferr_t status = fgdb_registers_serialize_one_with_thread(packet_buffer, i, thread);
		if (status != ferr_ok) {
			return status;
		}
	}

	return ferr_ok;
};

ferr_t fgdb_registers_serialize_one(fgdb_packet_buffer_t* packet_buffer, fthread_t* thread, uintmax_t id) {
	return fgdb_registers_serialize_one_with_thread(packet_buffer, id, thread);
};

ferr_t fgdb_registers_deserialize_many(fgdb_packet_buffer_t* packet_buffer, fthread_t* thread) {
	for (size_t i = 0; i < 24; ++i) {
		ferr_t status;

		if (packet_buffer->offset == packet_buffer->length) {
			break;
		}

		status = fgdb_registers_deserialize_one_with_thread(packet_buffer, i, thread);
		if (status != ferr_ok) {
			return status;
		}
	}

	return ferr_ok;
};

ferr_t fgdb_registers_deserialize_one(fgdb_packet_buffer_t* packet_buffer, fthread_t* thread, uintmax_t id) {
	return fgdb_registers_deserialize_one_with_thread(packet_buffer, id, thread);
};

void fgdb_registers_set_single_step(fthread_t* thread) {
	farch_int_frame_flat_registers_union_t* flat_frame = (void*)fint_current_frame();
	bool use_frame = !thread || thread == fthread_current(); // TODO: this is wrong when we implement multicore support

	if (use_frame) {
		flat_frame->flat.rflags |= 1ULL << 8;
	} else {
		thread->saved_context->rflags |= 1ULL << 8;
	}
};

void fgdb_registers_clear_single_step(fthread_t* thread) {
	farch_int_frame_flat_registers_union_t* flat_frame = (void*)fint_current_frame();
	bool use_frame = !thread || thread == fthread_current(); // TODO: this is wrong when we implement multicore support

	if (use_frame) {
		flat_frame->flat.rflags &= ~(1ULL << 8);\
	} else {
		thread->saved_context->rflags &= ~(1ULL << 8);
	}
};

void fgdb_registers_skip_breakpoint(void) {
	fint_frame_t* frame = fint_current_frame();

	if (!frame) {
		fpanic("Requested breakpoint skip, but no interrupt frame was available!");
	}

	frame->core.rip = (void*)((uintptr_t)frame->core.rip + 1);
};

ferr_t fgdb_registers_serialize_features(fgdb_packet_buffer_t* packet_buffer, const char* name, size_t name_length, size_t offset, size_t length) {
	ferr_t status = ferr_ok;
	char more = '\0';

	if (name_length != sizeof("target.xml") - 1 || simple_strncmp(name, "target.xml", sizeof("target.xml") - 1) != 0) {
		return ferr_no_such_resource;
	}

	more = (offset + length >= sizeof(target_xml_data)) ? 'l' : 'm';

	status = fgdb_packet_buffer_append(packet_buffer, (const uint8_t*)&more, 1);
	if (status != ferr_ok) {
		return status;
	}

	status = fgdb_packet_buffer_serialize_data(packet_buffer, &target_xml_data[offset], simple_min(sizeof(target_xml_data) - offset, length));
	if (status != ferr_ok) {
		return status;
	}

	return status;
};

ferr_t fgdb_registers_watchpoint_set(void* address, size_t size, fgdb_registers_watchpoint_type_t type) {
	uint64_t dr7;
	uint8_t index = 0;

	if (size > 8 || (type & (fgdb_registers_watchpoint_type_read | fgdb_registers_watchpoint_type_write)) == 0) {
		return ferr_invalid_argument;
	}

	__asm__ volatile("mov %%dr7, %0" : "=r" (dr7));

	for (; index < 4; ++index) {
		if ((dr7 & (1ULL << (index * 2 + 1))) == 0) {
			break;
		}
	}

	if (index == 4) {
		return ferr_temporary_outage;
	}

	switch (index) {
		case 0:
			__asm__ volatile("mov %0, %%dr0\n" :: "r" ((uintptr_t)address));
			break;
		case 1:
			__asm__ volatile("mov %0, %%dr1\n" :: "r" ((uintptr_t)address));
			break;
		case 2:
			__asm__ volatile("mov %0, %%dr2\n" :: "r" ((uintptr_t)address));
			break;
		case 3:
			__asm__ volatile("mov %0, %%dr3\n" :: "r" ((uintptr_t)address));
			break;
	}

	dr7 |= 1ULL << (index * 2 + 1);
	dr7 = (dr7 & ~(3ULL << (index * 4 + 16))) | ((((type & fgdb_registers_watchpoint_type_read) == 0) ? 1ULL : 3ULL) << (index * 4 + 16));
	dr7 = (dr7 & ~(3ULL << (index * 4 + 18))) | ((size < 2 ? 0ULL : (size < 4 ? 1ULL : (size < 8 ? 3ULL : 2ULL))) << (index * 4 + 18));

	//fconsole_logf("index = %u; dr7 = %zu\n", index, dr7);

	__asm__ volatile("mov %0, %%dr7" :: "r" (dr7));

	return ferr_ok;
};

ferr_t fgdb_registers_watchpoint_clear(void* address) {
	uint64_t dr7;
	uint8_t index = 0;
	uint64_t dr0;
	uint64_t dr1;
	uint64_t dr2;
	uint64_t dr3;

	__asm__ volatile("mov %%dr7, %0" : "=r" (dr7));

	__asm__ volatile(
		"mov %%dr0, %0\n"
		"mov %%dr1, %1\n"
		"mov %%dr2, %2\n"
		"mov %%dr3, %3\n"
		:
		"=r" (dr0),
		"=r" (dr1),
		"=r" (dr2),
		"=r" (dr3)
	);

	if ((dr7 & (1ULL << (index * 2 + 1))) == 0 || dr0 != (uintptr_t)address) {
		++index;
	}

	if ((dr7 & (1ULL << (index * 2 + 1))) == 0 || dr1 != (uintptr_t)address) {
		++index;
	}

	if ((dr7 & (1ULL << (index * 2 + 1))) == 0 || dr2 != (uintptr_t)address) {
		++index;
	}

	if ((dr7 & (1ULL << (index * 2 + 1))) == 0 || dr3 != (uintptr_t)address) {
		++index;
	}

	if (index == 4) {
		return ferr_no_such_resource;
	}

	dr7 &= ~((1ULL << (index * 2 + 1)) | (3ULL << (index * 4 + 16)) | (3ULL << (index * 4 + 18)));

	__asm__ volatile("mov %0, %%dr7" :: "r" (dr7));

	return ferr_ok;
};
