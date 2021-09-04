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
 * Minimal UBSan handlers for Ferro, inspired by compiler-rt's `ubsan_minimal_handlers.cpp`
 *
 * While Ferro already depends on compiler-rt for the builtins library, ubsan_minimal is unfortunately not minimal enough
 * to work without a few platform-specific modifications. So instead, we implement our own handlers.
 */

#include <ferro/core/console.h>
#include <ferro/core/panic.h>

/**
 * A common breakpoint for all UBSan handlers.
 *
 * All handlers must call this function on entry, so this is useful for setting debugger breakpoints for breaking on UB.
 */
FERRO_NEVER_INLINE void ubsan_common_breakpoint(void) {
	__asm__("");
};

#if UBSAN_MINIMAL
	#define UBSAN_RECOVERABLE_NAME(name) __ubsan_handle_ ## name ## _minimal
	#define UBSAN_UNRECOVERABLE_NAME(name) __ubsan_handle_ ## name ## _minimal_abort
#else
	#define UBSAN_RECOVERABLE_NAME(name) __ubsan_handle_ ## name
	#define UBSAN_UNRECOVERABLE_NAME(name) __ubsan_handle_ ## name ## _abort
#endif

#define UBSAN_HANDLER_RECOVERABLE_WRAPPER(name, message, parameters, args) \
	void UBSAN_RECOVERABLE_NAME(name) parameters { \
		ubsan_common_breakpoint(); \
		fconsole_log("ubsan: " message "\n"); \
		ubsan_handle_ ## name args; \
	};

#define UBSAN_HANDLER_UNRECOVERABLE_WRAPPER(name, message, parameters, args) \
	void UBSAN_UNRECOVERABLE_NAME(name) parameters { \
		UBSAN_RECOVERABLE_NAME(name) args; \
		fpanic(NULL); \
	};

#define UBSAN_HANDLER(name, message, parameters, args) \
	void ubsan_handle_ ## name parameters; \
	UBSAN_HANDLER_RECOVERABLE_WRAPPER(name, message, parameters, args); \
	UBSAN_HANDLER_UNRECOVERABLE_WRAPPER(name, message, parameters, args); \
	void ubsan_handle_ ## name parameters

FERRO_STRUCT(ubsan_source_location) {
	const char* filename;
	uint32_t line;
	uint32_t column;
};

FERRO_ENUM(uint16_t, ubsan_type_descriptor_kind) {
	ubsan_type_descriptor_kind_integer = 0x0000,
	ubsan_type_descriptor_kind_float   = 0x0001,
	ubsan_type_descriptor_kind_unknown = 0xffff,
};

FERRO_STRUCT(ubsan_type_descriptor) {
	ubsan_type_descriptor_kind_t kind;
	uint16_t info;
	char name[];
};

FERRO_ALWAYS_INLINE uint16_t ubsan_type_descriptor_bit_width(const ubsan_type_descriptor_t* descriptor) {
	switch (descriptor->kind) {
		case ubsan_type_descriptor_kind_integer: {
			return 1 << (descriptor->info >> 1);
		} break;

		case ubsan_type_descriptor_kind_float: {
			return descriptor->info;
		} break;

		default: {
			return 0;
		} break;
	}
};

FERRO_ALWAYS_INLINE bool ubsan_type_descriptor_is_signed_integer(const ubsan_type_descriptor_t* descriptor) {
	if (descriptor->kind != ubsan_type_descriptor_kind_integer) {
		return false;
	}
	return (descriptor->info & 1) != 0;
};

FERRO_ALWAYS_INLINE bool ubsan_type_descriptor_is_unsigned_integer(const ubsan_type_descriptor_t* descriptor) {
	if (descriptor->kind != ubsan_type_descriptor_kind_integer) {
		return false;
	}
	return (descriptor->info & 1) == 0;
};

static void ubsan_log_location(const ubsan_source_location_t* location, bool newline) {
	fconsole_logf("%s:%u:%u", location->filename, location->line, location->column);
	if (newline) {
		fconsole_log("\n");
	}
};

FERRO_ENUM(uint8_t, ubsan_type_mismatch_data_kind) {
	ubsan_type_mismatch_data_kind_load,
	ubsan_type_mismatch_data_kind_store,
	ubsan_type_mismatch_data_kind_refbind,
	ubsan_type_mismatch_data_kind_member_access,
	ubsan_type_mismatch_data_kind_member_call,
	ubsan_type_mismatch_data_kind_constructor_call,
	ubsan_type_mismatch_data_kind_downcast,
	ubsan_type_mismatch_data_kind_downcast2,
	ubsan_type_mismatch_data_kind_upcast,
	ubsan_type_mismatch_data_kind_virtual_cast,
	ubsan_type_mismatch_data_kind_nonnull,
	ubsan_type_mismatch_data_kind_dynamic,
};

const char* ubsan_type_mismatch_data_kind_names[] = {
	"load of",
	"store to",
	"reference binding to",
	"member access within",
	"member call on",
	"constructor call on",
	"downcast of",
	"downcast of",
	"upcast of",
	"cast to virtual base of",
	"_Nonnull binding to",
	"dynamic operation on",
};

FERRO_STRUCT(ubsan_type_mismatch_data) {
	ubsan_source_location_t location;
	const ubsan_type_descriptor_t* type;
	uint8_t log_of_alignment;
	ubsan_type_mismatch_data_kind_t kind;
};

UBSAN_HANDLER(
#if UBSAN_MINIMAL
	type_mismatch,
#else
	type_mismatch_v1,
#endif
	"type-mismatch", (const ubsan_type_mismatch_data_t* data, uintptr_t pointer), (data, pointer)) {
	uintptr_t alignment = 1ULL << data->log_of_alignment;

	fconsole_logf("ubsan: type mismatch on %s %p; ", ubsan_type_mismatch_data_kind_names[data->kind], (void*)pointer);

	if (pointer == 0) {
		fconsole_log("null pointer access; ");
	} else if ((pointer & (alignment - 1)) != 0) {
		fconsole_logf("misaligned access (requires alignment of %lu); ", alignment);
	} else {
		fconsole_logf("insufficient space for object of type %s; ", data->type->name);
	}

	ubsan_log_location(&data->location, true);
};

UBSAN_HANDLER(alignment_assumption, "alignment-assumption", (), ()) {

};

UBSAN_HANDLER(add_overflow, "add-overflow", (), ()) {

};

UBSAN_HANDLER(sub_overflow, "sub-overflow", (), ()) {

};

UBSAN_HANDLER(mul_overflow, "mul-overflow", (), ()) {

};

UBSAN_HANDLER(negate_overflow, "negate-overflow", (), ()) {

};

UBSAN_HANDLER(divrem_overflow, "divrem-overflow", (), ()) {

};

UBSAN_HANDLER(shift_out_of_bounds, "shift-out-of-bounds", (), ()) {

};

UBSAN_HANDLER(out_of_bounds, "out-of-bounds", (), ()) {

};

UBSAN_HANDLER(builtin_unreachable, "builtin-unreachable", (), ()) {

};

UBSAN_HANDLER(missing_return, "missing-return", (), ()) {

};

UBSAN_HANDLER(vla_bound_not_positive, "vla-bound-not-positive", (), ()) {

};

UBSAN_HANDLER(float_cast_overflow, "float-cast-overflow", (), ()) {

};

UBSAN_HANDLER(load_invalid_value, "load-invalid-value", (), ()) {

};

UBSAN_HANDLER(invalid_builtin, "invalid-builtin", (), ()) {

};

UBSAN_HANDLER(invalid_objc_cast, "invalid-objc-cast", (), ()) {

};

UBSAN_HANDLER(function_type_mismatch, "function-type-mismatch", (), ()) {

};

UBSAN_HANDLER(implicit_conversion, "implicit-conversion", (), ()) {

};

UBSAN_HANDLER(nonnull_arg, "nonnull-arg", (), ()) {

};

UBSAN_HANDLER(nonnull_return, "nonnull-return", (), ()) {

};

UBSAN_HANDLER(nullability_arg, "nullability-arg", (), ()) {

};

UBSAN_HANDLER(nullability_return, "nullability-return", (), ()) {

};

UBSAN_HANDLER(pointer_overflow, "pointer-overflow", (), ()) {

};

UBSAN_HANDLER(cfi_check_fail, "cfi-check-fail", (), ()) {

};
