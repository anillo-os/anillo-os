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

#include <libspooky/deserializer.private.h>
#include <ferro/bits.h>
#include <libspooky/types.private.h>
#include <libspooky/structure.h>
#include <libspooky/function.h>
#include <libspooky/proxy.h>

ferr_t spooky_deserializer_init(spooky_deserializer_t* deserializer, sys_channel_message_t* message) {
	deserializer->message = message;
	deserializer->data = sys_channel_message_data(message);
	deserializer->length = sys_channel_message_length(message);
	deserializer->offset = 0;
	return ferr_ok;
};

ferr_t spooky_deserializer_skip(spooky_deserializer_t* deserializer, size_t offset, size_t* out_offset, size_t length) {
	ferr_t status = ferr_ok;
	size_t extra_len = 0;

	if (offset > deserializer->length) {
		offset = deserializer->offset;
	}

	if ((deserializer->offset - offset) < length) {
		extra_len = length - (deserializer->offset - offset);
	}

	if (extra_len > 0) {
		if (deserializer->offset + extra_len > deserializer->length) {
			status = ferr_too_big;
			goto out;
		}

		deserializer->offset += extra_len;
	}

	if (out_offset) {
		*out_offset = offset;
	}

out:
	return status;
};

ferr_t spooky_deserializer_decode_integer(spooky_deserializer_t* deserializer, size_t offset, size_t* out_offset, void* out_value, size_t max_value_length, size_t* out_encoded_length, bool is_signed) {
	ferr_t status = ferr_ok;
	uint64_t value = 0;
	size_t length = 0;
	uint8_t bits_in_use = 0;

	if (offset > deserializer->length) {
		offset = deserializer->offset;
	}

	if (max_value_length > 8) {
		max_value_length = 8;
	}

	// first, rebuild the value
	while (true) {
		uint8_t byte;

		if (offset >= deserializer->length) {
			status = ferr_invalid_argument;
			goto out;
		}

		byte = deserializer->data[offset + length];
		++length;

		if (length == 9) {
			// this is the final byte
			// we use all 8 bits in this byte for data
			value |= (uint64_t)byte << 56;
		} else {
			value |= (uint64_t)(byte & 0x7f) << (7 * (length - 1));
		}

		if (length == 9 || (byte & (1 << 7)) == 0) {
			break;
		}
	}

	// now determine how many bits are in-use and whether this value fits in the user-provided buffer
	// (this will also take the sign bit into account if the number is signed)
	bits_in_use = ferro_bits_in_use_u64(value);

	if (bits_in_use == 0) {
		// we always need at least 1 bit
		bits_in_use = 1;
	}

	if (bits_in_use > max_value_length * 8) {
		status = ferr_too_big;
		goto out;
	}

	// now handle signed numbers properly
	if (is_signed) {
		bool is_neg = (value & 1) != 0;
		value >>= 1;
		if (is_neg) {
			// negate it
			value = ~value + 1;
		}
	}

#if FERRO_ENDIANNESS == FERRO_ENDIANNESS_BIG
	// big endian: remove leading zeros (or sign-extended ones)
	simple_memcpy(out_value, &((char*)&value)[sizeof(value) - max_length], max_length);
#else
	// little endian: remove trailing zeros (or sign-extended ones)
	simple_memcpy(out_value, &value, max_value_length);
#endif

	// this shouldn't fail
	LIBSPOOKY_WUR_IGNORE(spooky_deserializer_skip(deserializer, offset, NULL, length));

	if (out_offset) {
		*out_offset = offset;
	}

	if (out_encoded_length) {
		*out_encoded_length = length;
	}

out:
	return status;
};

ferr_t spooky_deserializer_decode_type(spooky_deserializer_t* deserializer, size_t offset, size_t* out_offset, size_t* out_length, spooky_type_t** out_type) {
	ferr_t status = ferr_ok;
	size_t start_offset = 0;
	size_t len = 0;
	spooky_type_tag_t tag = spooky_type_tag_invalid;

	status = spooky_deserializer_skip(deserializer, offset, &offset, 1);
	if (status != ferr_ok) {
		goto out;
	}

	start_offset = offset;

	tag = deserializer->data[offset];
	offset += 1;

	switch (tag) {
		#define BASIC_TAG_CASE(_typecode) \
			case spooky_type_tag_ ## _typecode: { \
				if (out_type) { \
					*out_type = spooky_type_ ## _typecode(); \
				} \
			} break;

		BASIC_TAG_CASE(data);
		BASIC_TAG_CASE(u8);
		BASIC_TAG_CASE(u16);
		BASIC_TAG_CASE(u32);
		BASIC_TAG_CASE(u64);
		BASIC_TAG_CASE(i8);
		BASIC_TAG_CASE(i16);
		BASIC_TAG_CASE(i32);
		BASIC_TAG_CASE(i64);
		BASIC_TAG_CASE(bool);
		BASIC_TAG_CASE(f32);
		BASIC_TAG_CASE(f64);
		BASIC_TAG_CASE(proxy);
		BASIC_TAG_CASE(channel);

		case spooky_type_tag_function:
		case spooky_type_tag_nowait_function: {
			size_t parameter_count = 0;
			spooky_function_parameter_t* parameters = NULL;
			size_t i = 0;

			status = spooky_deserializer_decode_integer(deserializer, offset, &offset, &parameter_count, sizeof(parameter_count), &len, false);
			if (status != ferr_ok) {
				goto out;
			}
			offset += len;

			if (out_type) {
				status = sys_mempool_allocate(sizeof(*parameters) * parameter_count, NULL, (void*)&parameters);
				if (status != ferr_ok) {
					goto out;
				}

				simple_memset(parameters, 0, sizeof(*parameters) * parameter_count);
			}

			for (; i < parameter_count; ++i) {
				status = spooky_deserializer_decode_integer(deserializer, offset, &offset, out_type ? &parameters[i].direction : NULL, sizeof(parameters[i].direction), &len, false);
				if (status != ferr_ok) {
					goto out;
				}
				offset += len;

				status = spooky_deserializer_decode_type(deserializer, offset, &offset, &len, out_type ? &parameters[i].type : NULL);
				if (status != ferr_ok) {
					break;
				}
				offset += len;
			}

			if (out_type) {
				if (status == ferr_ok) {
					status = spooky_function_create(tag == spooky_type_tag_function, parameters, parameter_count, out_type);
				}

				for (size_t j = 0; j < i; ++j) {
					if (parameters[j].type) {
						spooky_release(parameters[j].type);
					}
				}

				LIBSPOOKY_WUR_IGNORE(sys_mempool_free(parameters));
			}
		} break;

		case spooky_type_tag_structure: {
			size_t total_byte_size = 0;
			size_t member_count = 0;
			spooky_structure_member_t* members = NULL;
			size_t i = 0;

			status = spooky_deserializer_decode_integer(deserializer, offset, &offset, &total_byte_size, sizeof(total_byte_size), &len, false);
			if (status != ferr_ok) {
				goto out;
			}
			offset += len;

			status = spooky_deserializer_decode_integer(deserializer, offset, &offset, &member_count, sizeof(member_count), &len, false);
			if (status != ferr_ok) {
				goto out;
			}
			offset += len;

			if (out_type) {
				status = sys_mempool_allocate(sizeof(*members) * member_count, NULL, (void*)&members);
				if (status != ferr_ok) {
					goto out;
				}

				simple_memset(members, 0, sizeof(*members) * member_count);
			}

			for (; i < member_count; ++i) {
				status = spooky_deserializer_decode_integer(deserializer, offset, &offset, out_type ? &members[i].offset : NULL, sizeof(members[i].offset), &len, false);
				if (status != ferr_ok) {
					break;
				}
				offset += len;

				status = spooky_deserializer_decode_type(deserializer, offset, &offset, &len, out_type ? &members[i].type : NULL);
				if (status != ferr_ok) {
					break;
				}
				offset += len;
			}

			if (out_type) {
				if (status == ferr_ok) {
					status = spooky_structure_create(total_byte_size, members, member_count, out_type);
				}

				for (size_t j = 0; j < i; ++j) {
					if (members[j].type) {
						spooky_release(members[j].type);
					}
				}

				LIBSPOOKY_WUR_IGNORE(sys_mempool_free(members));
			}
		} break;

		default:
			status = ferr_invalid_argument;
			goto out;
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

ferr_t spooky_deserializer_decode_data_object(spooky_deserializer_t* deserializer, size_t offset, size_t* out_offset, size_t* out_length, sys_data_t** out_data) {
	sys_channel_message_attachment_index_t index = sys_channel_message_attachment_index_invalid;
	ferr_t status = spooky_deserializer_skip(deserializer, offset, &offset, sizeof(index));

	if (status != ferr_ok) {
		goto out;
	}

	simple_memcpy(&index, deserializer->data + offset, sizeof(index));

	if (index != sys_channel_message_attachment_index_invalid) {
		status = sys_channel_message_detach_data(deserializer->message, index, out_data);
		if (status != ferr_ok) {
			goto out;
		}
	} else if (out_data) {
		*out_data = NULL;
	}

	if (out_offset) {
		*out_offset = offset;
	}

	if (out_length) {
		*out_length = sizeof(index);
	}

out:
	return status;
};

ferr_t spooky_deserializer_decode_channel(spooky_deserializer_t* deserializer, size_t offset, size_t* out_offset, size_t* out_length, sys_channel_t** out_channel) {
	sys_channel_message_attachment_index_t index = sys_channel_message_attachment_index_invalid;
	ferr_t status = spooky_deserializer_skip(deserializer, offset, &offset, sizeof(index));

	if (status != ferr_ok) {
		goto out;
	}

	simple_memcpy(&index, deserializer->data + offset, sizeof(index));

	if (index != sys_channel_message_attachment_index_invalid) {
		status = sys_channel_message_detach_channel(deserializer->message, index, out_channel);
		if (status != ferr_ok) {
			goto out;
		}
	} else if (out_channel) {
		*out_channel = NULL;
	}

	if (out_offset) {
		*out_offset = offset;
	}

	if (out_length) {
		*out_length = sizeof(index);
	}

out:
	return status;
};
