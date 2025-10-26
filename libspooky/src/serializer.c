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

#include <libspooky/serializer.private.h>
#include <libspooky/types.private.h>
#include <libspooky/function.private.h>
#include <libspooky/structure.private.h>
#include <ferro/bits.h>
#include <libspooky/proxy.h>

ferr_t spooky_serializer_init(spooky_serializer_t* serializer) {
	serializer->length = 0;
	return sys_channel_message_create(0, &serializer->message);
};

ferr_t spooky_serializer_finalize(spooky_serializer_t* serializer, sys_channel_message_t** out_message) {
	if (out_message) {
		*out_message = serializer->message;
	} else {
		sys_release(serializer->message);
	}
	serializer->message = NULL;
	serializer->length = 0;
	return ferr_ok;
};

ferr_t spooky_serializer_reserve(spooky_serializer_t* serializer, size_t offset, size_t* out_offset, size_t length) {
	ferr_t status = ferr_ok;
	size_t extra_len = 0;

	if (offset > serializer->length) {
		offset = serializer->length;
	}

	if ((serializer->length - offset) < length) {
		extra_len = length - (serializer->length - offset);
	}

	if (extra_len > 0) {
		status = sys_channel_message_extend(serializer->message, extra_len);
		if (status != ferr_ok) {
			goto out;
		}
		serializer->length += extra_len;
	}

	if (out_offset) {
		*out_offset = offset;
	}

out:
	return status;
};

ferr_t spooky_serializer_encode_integer(spooky_serializer_t* serializer, size_t offset, size_t* out_offset, const void* value, size_t length, size_t* out_length, bool is_signed) {
	ferr_t status = ferr_ok;
	size_t start_offset = offset;
	size_t groups_of_7 = 0;
	uint64_t val = 0;
	uint8_t bits_in_use = 0;
	bool is_neg = false;
	char* data = NULL;

	if (length > sizeof(val)) {
		status = ferr_invalid_argument;
		goto out;
	}

	// copy the value into the biggest integer we can allocate so we can work with it
#if FERRO_ENDIANNESS == FERRO_ENDIANNESS_BIG
	// big endian: add leading zeros
	simple_memcpy(&((char*)&val)[sizeof(val) - length], value, length);
#else
	// little endian: add trailing zeros
	simple_memcpy(&val, value, length);
#endif

	if (is_signed) {
		uint64_t msbit = 1ull << ((length * 8) - 1);
		if (val & msbit) {
			// this number is negative; negate it to make it positive
			uint64_t len_mask = UINT64_MAX >> ((sizeof(uint64_t) - length) * 8);
			is_neg = true;
			val = (~val + 1) & len_mask;
		}

		// encode the sign bit as the LSBit
		// note that signed integers only ever have up to 63 bits of magnitude,
		// so we never violate the 64-bit maximum assumed by this encoding.
		val = (val << 1) | (is_neg ? 1 : 0);
	}

	// determine how many bytes we need to store it
	bits_in_use = ferro_bits_in_use_u64(val);

	if (bits_in_use == 0) {
		// we always have to occupy at least 1 bit
		bits_in_use = 1;
	}

	// 64 is a special case because we only use groups of 7 bits for the
	// first 8 bytes. since we cannot have more than 64 bits in an integer,
	// the 9th byte doesn't have a continuation bit, so we can use that full byte.
	groups_of_7 = bits_in_use == 64 ? 9 : ((bits_in_use + 6) / 7);

	// now allocate space
	status = spooky_serializer_reserve(serializer, offset, &offset, groups_of_7);
	if (status != ferr_ok) {
		goto out;
	}

	start_offset = offset;

	data = sys_channel_message_data(serializer->message);

	if (val == 0) {
		// special case: encode a single zero byte
		data[offset] = 0;
	}

	// now encode the first 8 groups of 7 bits
	for (size_t i = 0; i < 8; ++i) {
		if (val == 0) {
			break;
		}
		uint8_t group = val & 0x7f;
		val >>= 7;
		data[offset + i] = group | (val == 0 ? 0 : 0x80);
	}

	// now encode the 9th byte (a full 8 bits), if necessary
	if (val != 0) {
		data[offset + 8] = val & 0xff;
	}

	if (out_offset) {
		*out_offset = start_offset;
	}

	if (out_length) {
		*out_length = groups_of_7;
	}

out:
	return status;
};

ferr_t spooky_serializer_encode_type(spooky_serializer_t* serializer, size_t offset, size_t* out_offset, size_t* out_length, spooky_type_t* type) {
	ferr_t status = ferr_ok;
	const spooky_object_class_t* type_class = spooky_object_class(type);
	spooky_type_tag_t tag = spooky_type_tag_invalid;
	size_t start_offset;

	if (offset > serializer->length) {
		offset = serializer->length;
	}

	#define BASIC_TYPE_TAG(_typecode) else if (type == spooky_type_ ## _typecode()) { \
			tag = spooky_type_tag_ ## _typecode; \
		}

	if (type_class == spooky_object_class_function()) {
		tag = ((spooky_function_object_t*)type)->wait ? spooky_type_tag_function : spooky_type_tag_nowait_function;
	} else if (type_class == spooky_object_class_structure()) {
		tag = spooky_type_tag_structure;
	}
	BASIC_TYPE_TAG(data)
	BASIC_TYPE_TAG(u8)
	BASIC_TYPE_TAG(u16)
	BASIC_TYPE_TAG(u32)
	BASIC_TYPE_TAG(u64)
	BASIC_TYPE_TAG(i8)
	BASIC_TYPE_TAG(i16)
	BASIC_TYPE_TAG(i32)
	BASIC_TYPE_TAG(i64)
	BASIC_TYPE_TAG(bool)
	BASIC_TYPE_TAG(f32)
	BASIC_TYPE_TAG(f64)
	BASIC_TYPE_TAG(proxy)
	BASIC_TYPE_TAG(channel)

	// TODO: optimize for space-efficiency by de-duplicating types

	status = spooky_serializer_reserve(serializer, offset, &offset, sizeof(tag));
	if (status != ferr_ok) {
		goto out;
	}

	start_offset = offset;

	((char*)sys_channel_message_data(serializer->message))[offset] = tag;
	offset += 1;

	if (tag == spooky_type_tag_function || tag == spooky_type_tag_nowait_function) {
		spooky_function_object_t* function = (void*)type;
		size_t len = 0;

		status = spooky_serializer_encode_integer(serializer, offset, &offset, &function->parameter_count, sizeof(function->parameter_count), &len, false);
		if (status != ferr_ok) {
			goto out;
		}
		offset += len;

		for (size_t i = 0; i < function->parameter_count; ++i) {
			status = spooky_serializer_encode_integer(serializer, offset, &offset, &function->parameters[i].direction, sizeof(function->parameters[i].direction), &len, false);
			if (status != ferr_ok) {
				goto out;
			}
			offset += len;

			status = spooky_serializer_encode_type(serializer, offset, &offset, &len, function->parameters[i].type);
			if (status != ferr_ok) {
				goto out;
			}
			offset += len;
		}
	} else if (tag == spooky_type_tag_structure) {
		spooky_structure_object_t* structure = (void*)type;
		size_t len = 0;

		status = spooky_serializer_encode_integer(serializer, offset, &offset, &structure->base.byte_size, sizeof(structure->base.byte_size), &len, false);
		if (status != ferr_ok) {
			goto out;
		}
		offset += len;

		status = spooky_serializer_encode_integer(serializer, offset, &offset, &structure->member_count, sizeof(structure->member_count), &len, false);
		if (status != ferr_ok) {
			goto out;
		}
		offset += len;

		for (size_t i = 0; i < structure->member_count; ++i) {
			status = spooky_serializer_encode_integer(serializer, offset, &offset, &structure->members[i].offset, sizeof(structure->members[i].offset), &len, false);
			if (status != ferr_ok) {
				goto out;
			}
			offset += len;

			status = spooky_serializer_encode_type(serializer, offset, &offset, &len, structure->members[i].type);
			if (status != ferr_ok) {
				goto out;
			}
			offset += len;
		}
	}

	if (out_offset) {
		*out_offset = start_offset;
	}

	if (out_length) {
		*out_length = offset - start_offset;
	}

out:
	return status;
};

ferr_t spooky_serializer_encode_data(spooky_serializer_t* serializer, size_t offset, size_t* out_offset, size_t length, const void* data) {
	char* msg_data = NULL;
	ferr_t status = spooky_serializer_reserve(serializer, offset, &offset, length);

	if (status != ferr_ok) {
		goto out;
	}

	msg_data = sys_channel_message_data(serializer->message);
	simple_memcpy(msg_data + offset, data, length);

	if (out_offset) {
		*out_offset = offset;
	}

out:
	return status;
};

ferr_t spooky_serializer_encode_data_object(spooky_serializer_t* serializer, size_t offset, size_t* out_offset, size_t* out_length, sys_data_t* data) {
	ferr_t status = spooky_serializer_reserve(serializer, offset, &offset, sizeof(sys_channel_message_attachment_index_t));
	sys_channel_message_attachment_index_t index = sys_channel_message_attachment_index_invalid;
	char* msg_data = NULL;

	if (status != ferr_ok) {
		goto out;
	}

	if (data) {
		status = sys_channel_message_attach_data(serializer->message, data, false, &index);
		if (status != ferr_ok) {
			goto out;
		}
	}

	// can't use spooky_serializer_encode_integer() because we don't want to attach the channel until we know
	// that we have space in the message data buffer to store the index (and we would need to know the index before
	// calling spooky_serializer_encode_integer())
	msg_data = sys_channel_message_data(serializer->message);
	simple_memcpy(msg_data + offset, &index, sizeof(index));

	if (out_offset) {
		*out_offset = offset;
	}

	if (out_length) {
		*out_length = sizeof(sys_channel_message_attachment_index_t);
	}

out:
	return status;
};

ferr_t spooky_serializer_encode_channel(spooky_serializer_t* serializer, size_t offset, size_t* out_offset, size_t* out_length, sys_channel_t* channel) {
	ferr_t status = spooky_serializer_reserve(serializer, offset, &offset, sizeof(sys_channel_message_attachment_index_t));
	sys_channel_message_attachment_index_t index = sys_channel_message_attachment_index_invalid;
	char* msg_data = NULL;

	if (status != ferr_ok) {
		goto out;
	}

	if (channel) {
		status = sys_channel_message_attach_channel(serializer->message, channel, &index);
		if (status != ferr_ok) {
			goto out;
		}
	}

	// can't use spooky_serializer_encode_integer() because we don't want to attach the channel until we know
	// that we have space in the message data buffer to store the index (and we would need to know the index before
	// calling spooky_serializer_encode_integer())
	msg_data = sys_channel_message_data(serializer->message);
	simple_memcpy(msg_data + offset, &index, sizeof(index));

	if (out_offset) {
		*out_offset = offset;
	}

	if (out_length) {
		*out_length = sizeof(sys_channel_message_attachment_index_t);
	}

out:
	return status;
};
