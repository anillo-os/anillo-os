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

#include <libspooky/invocation.private.h>
#include <libspooky/function.private.h>
#include <libspooky/structure.private.h>
#include <libspooky/serializer.private.h>
#include <libspooky/deserializer.private.h>

// TODO: proper support for nowait functions
//       (they're supposed to respond immediately upon receiving the incoming invocation)

static void spooky_invocation_release_object(const void* object, spooky_type_t* type) {
	if (type == spooky_type_data()) {
		spooky_data_t* data = *(spooky_data_t* const*)object;

		if (data) {
			spooky_release(data);
		}
	} else if (spooky_object_class(type) == spooky_object_class_structure()) {
		spooky_structure_object_t* structure = (void*)type;

		for (size_t i = 0; i < structure->member_count; ++i) {
			spooky_invocation_release_object((const char*)object + structure->members[i].offset, structure->members[i].type);
		}
	}
};

static ferr_t spooky_invocation_retain_object(const void* object, spooky_type_t* type) {
	if (type == spooky_type_data()) {
		spooky_data_t* data = *(spooky_data_t* const*)object;

		if (data) {
			return spooky_retain(data);
		}
	} else if (spooky_object_class(type) == spooky_object_class_structure()) {
		spooky_structure_object_t* structure = (void*)type;

		for (size_t i = 0; i < structure->member_count; ++i) {
			ferr_t status = spooky_invocation_retain_object((char*)object + structure->members[i].offset, structure->members[i].type);
			if (status != ferr_ok) {
				// release all members that we successfully retained
				for (size_t j = 0; j < i; ++j) {
					spooky_invocation_release_object((const char*)object + structure->members[j].offset, structure->members[j].type);
				}
				return status;
			}
		}
	}

	return ferr_ok;
};

static ferr_t spooky_invocation_serialize_object(spooky_invocation_object_t* invocation, spooky_serializer_t* serializer, const void* object, spooky_type_t* type_obj, size_t param_index) {
	spooky_type_object_t* type = (void*)type_obj;

	#define BASIC_SERIALIZE(_type, _typecode, _is_signed) \
		if (type_obj == spooky_type_ ## _typecode()) { \
			return spooky_serializer_encode_integer(serializer, UINT64_MAX, NULL, object, sizeof(_type), NULL, _is_signed); \
		}

	BASIC_SERIALIZE(uint8_t, u8, false);
	BASIC_SERIALIZE(uint16_t, u16, false);
	BASIC_SERIALIZE(uint32_t, u32, false);
	BASIC_SERIALIZE(uint64_t, u64, false);
	BASIC_SERIALIZE(int8_t, i8, true);
	BASIC_SERIALIZE(int16_t, i16, true);
	BASIC_SERIALIZE(int32_t, i32, true);
	BASIC_SERIALIZE(int64_t, i64, true);
	BASIC_SERIALIZE(bool, bool, false);

	if (type_obj == spooky_type_f32() || type_obj == spooky_type_f64()) {
		// these are just encoded in fixed space because they typically occupy many of the higher bits,
		// so doing encoding on them to try to minimize their space would result in almost no difference
		size_t offset = UINT64_MAX;
		bool is_f32 = type_obj == spooky_type_f32();
		size_t length = is_f32 ? sizeof(float) : sizeof(double);
		ferr_t status = spooky_serializer_reserve(serializer, offset, &offset, length);
		if (status != ferr_ok) {
			return status;
		}
		simple_memcpy(&serializer->data[offset], object, length);
		return ferr_ok;
	}

	if (type_obj == spooky_type_data()) {
		spooky_data_t* data = *(spooky_data_t**)object;
		size_t offset = UINT64_MAX;
		size_t length = spooky_data_length(data);
		ferr_t status = spooky_serializer_encode_integer(serializer, offset, NULL, &length, sizeof(length), NULL, false);
		if (status != ferr_ok) {
			return status;
		}
		status = spooky_serializer_reserve(serializer, offset, &offset, length);
		if (status != ferr_ok) {
			return status;
		}
		simple_memcpy(&serializer->data[offset], spooky_data_contents(data), length);
		return status;
	}

	if (spooky_object_class(type_obj) == spooky_object_class_structure()) {
		spooky_structure_object_t* structure = (void*)type_obj;
		size_t offset = 0;
		for (size_t i = 0; i < structure->member_count; ++i) {
			ferr_t status = spooky_invocation_serialize_object(invocation, serializer, &((char*)object)[structure->members[i].offset], structure->members[i].type, param_index);
			if (status != ferr_ok) {
				return status;
			}
		}
		return ferr_ok;
	}

	if (spooky_object_class(type_obj) == spooky_object_class_function()) {
		for (size_t i = 0; i < invocation->outgoing_callback_info_count; ++i) {
			spooky_invocation_outgoing_callback_info_t* callback_info = &invocation->outgoing_callback_infos[i];
			if (callback_info->index == param_index) {
				return spooky_serializer_encode_integer(serializer, UINT64_MAX, NULL, &callback_info->conversation_id, sizeof(callback_info->conversation_id), NULL, false);
			}
		}
		return ferr_no_such_resource;
	}

	return ferr_unknown;
};

static ferr_t spooky_invocation_deserialize_object(spooky_invocation_object_t* invocation, spooky_deserializer_t* deserializer, void* object, spooky_type_t* type_obj, size_t param_index) {
	spooky_type_object_t* type = (void*)type_obj;

	#define BASIC_DESERIALIZE(_type, _typecode, _is_signed) \
		if (type_obj == spooky_type_ ## _typecode()) { \
			return spooky_deserializer_decode_integer(deserializer, UINT64_MAX, NULL, object, sizeof(_type), NULL, _is_signed); \
		}

	BASIC_DESERIALIZE(uint8_t, u8, false);
	BASIC_DESERIALIZE(uint16_t, u16, false);
	BASIC_DESERIALIZE(uint32_t, u32, false);
	BASIC_DESERIALIZE(uint64_t, u64, false);
	BASIC_DESERIALIZE(int8_t, i8, true);
	BASIC_DESERIALIZE(int16_t, i16, true);
	BASIC_DESERIALIZE(int32_t, i32, true);
	BASIC_DESERIALIZE(int64_t, i64, true);
	BASIC_DESERIALIZE(bool, bool, false);

	if (type_obj == spooky_type_f32() || type_obj == spooky_type_f64()) {
		size_t offset = UINT64_MAX;
		bool is_f32 = type_obj == spooky_type_f32();
		size_t length = is_f32 ? sizeof(float) : sizeof(double);
		ferr_t status = spooky_deserializer_skip(deserializer, offset, &offset, length);
		if (status != ferr_ok) {
			return status;
		}
		simple_memcpy(object, &deserializer->data[offset], length);
		return ferr_ok;
	}

	if (type_obj == spooky_type_data()) {
		spooky_data_t* data = NULL;
		size_t offset = UINT64_MAX;
		size_t length = 0;
		ferr_t status = spooky_deserializer_decode_integer(deserializer, offset, NULL, &length, sizeof(length), NULL, false);
		if (status != ferr_ok) {
			return status;
		}
		status = spooky_deserializer_skip(deserializer, offset, &offset, length);
		if (status != ferr_ok) {
			return status;
		}
		status = spooky_data_create(&deserializer->data[offset], length, &data);
		if (status != ferr_ok) {
			return status;
		}
		*(spooky_data_t**)object = data;
		return status;
	}

	if (spooky_object_class(type_obj) == spooky_object_class_structure()) {
		spooky_structure_object_t* structure = (void*)type_obj;
		size_t offset = 0;
		for (size_t i = 0; i < structure->member_count; ++i) {
			ferr_t status = spooky_invocation_deserialize_object(invocation, deserializer, &((char*)object)[structure->members[i].offset], structure->members[i].type, param_index);
			if (status != ferr_ok) {
				return status;
			}
		}
		return ferr_ok;
	}

	if (spooky_object_class(type_obj) == spooky_object_class_function()) {
		for (size_t i = 0; i < invocation->incoming_callback_info_count; ++i) {
			spooky_invocation_incoming_callback_info_t* callback_info = &invocation->incoming_callback_infos[i];
			if (callback_info->index == param_index) {
				return spooky_deserializer_decode_integer(deserializer, UINT64_MAX, NULL, &callback_info->conversation_id, sizeof(callback_info->conversation_id), NULL, false);
			}
		}
		return ferr_no_such_resource;
	}

	return ferr_unknown;
};

static void spooky_invocation_destroy(spooky_object_t* obj) {
	spooky_invocation_object_t* invocation = (void*)obj;

	if (invocation->incoming_data) {
		spooky_function_object_t* func = (void*)invocation->function_type;
		size_t offset = 0;

		for (size_t i = 0; i < func->parameter_count; ++i) {
			spooky_function_parameter_info_t* param_info = &func->parameters[i];
			spooky_type_object_t* param_type = (void*)param_info->type;

			if (param_info->direction == (invocation->incoming ? spooky_function_parameter_direction_out : spooky_function_parameter_direction_in)) {
				continue;
			}

			spooky_invocation_release_object(invocation->incoming_data + offset, (void*)param_type);

			offset += param_type->byte_size;
		}

		LIBSPOOKY_WUR_IGNORE(sys_mempool_free(invocation->incoming_data));
	}

	if (invocation->outgoing_data) {
		spooky_function_object_t* func = (void*)invocation->function_type;
		size_t offset = 0;

		for (size_t i = 0; i < func->parameter_count; ++i) {
			spooky_function_parameter_info_t* param_info = &func->parameters[i];
			spooky_type_object_t* param_type = (void*)param_info->type;

			if (param_info->direction == (invocation->incoming ? spooky_function_parameter_direction_in : spooky_function_parameter_direction_out)) {
				continue;
			}

			spooky_invocation_release_object(invocation->outgoing_data + offset, (void*)param_type);

			offset += param_type->byte_size;
		}

		LIBSPOOKY_WUR_IGNORE(sys_mempool_free(invocation->outgoing_data));
	}

	if (invocation->function_type) {
		spooky_release(invocation->function_type);
	}

	if (invocation->channel) {
		eve_release(invocation->channel);
	}

	if (invocation->incoming_callback_infos) {
		LIBSPOOKY_WUR_IGNORE(sys_mempool_free(invocation->incoming_callback_infos));
	}

	if (invocation->outgoing_callback_infos) {
		for (size_t i = 0; i < invocation->outgoing_callback_info_count; ++i) {
			spooky_invocation_outgoing_callback_info_t* callback_info = &invocation->outgoing_callback_infos[i];

			if (!callback_info->implementation) {
				continue;
			}

			// FIXME: this should run in a loop work item
			callback_info->implementation(callback_info->context, NULL);
		}

		LIBSPOOKY_WUR_IGNORE(sys_mempool_free(invocation->outgoing_callback_infos));
	}

	if (invocation->name) {
		LIBSPOOKY_WUR_IGNORE(sys_mempool_free(invocation->name));
	}
};

static const spooky_object_class_t invocation_class = {
	LIBSYS_OBJECT_CLASS_INTERFACE(NULL),
	.destroy = spooky_invocation_destroy,
};

static void spooky_invocation_function_data_lengths(spooky_function_t* function, size_t* out_input_data_length, size_t* out_output_data_length, size_t* out_input_callback_count, size_t* out_output_callback_count) {
	spooky_function_object_t* func = (void*)function;

	for (size_t i = 0; i < func->parameter_count; ++i) {
		spooky_function_parameter_info_t* param_info = &func->parameters[i];
		spooky_type_object_t* param_type = (void*)param_info->type;

		if (param_info->direction == spooky_function_parameter_direction_in) {
			if (out_input_data_length) {
				*out_input_data_length += param_type->byte_size;
			}

			if (out_input_callback_count && spooky_object_class((void*)param_type) == spooky_object_class_function()) {
				++(*out_input_callback_count);
			}
		} else {
			if (out_output_data_length) {
				*out_output_data_length += param_type->byte_size;
			}

			if (out_output_callback_count && spooky_object_class((void*)param_type) == spooky_object_class_function()) {
				++(*out_output_callback_count);
			}
		}
	}
};

ferr_t spooky_invocation_create(const char* name, size_t name_length, spooky_function_t* function, eve_channel_t* channel, spooky_invocation_t** out_invocation) {
	ferr_t status = ferr_ok;
	spooky_invocation_object_t* invocation = NULL;
	sys_channel_t* sys_channel = NULL;
	spooky_function_object_t* func = (void*)function;
	size_t callback_info_index = 0;
	size_t incoming_data_length = 0;
	size_t outgoing_data_length = 0;

	if (spooky_retain(function) != ferr_ok) {
		function = NULL;
		channel = NULL;
		status = ferr_permanent_outage;
		goto out;
	}

	if (eve_retain(channel) != ferr_ok) {
		channel = NULL;
		status = ferr_permanent_outage;
		goto out;
	}

	status = eve_channel_target(channel, false, &sys_channel);
	if (status != ferr_ok) {
		goto out;
	}

	status = sys_object_new(&invocation_class, sizeof(*invocation) - sizeof(invocation->object), (void*)&invocation);
	if (status != ferr_ok) {
		goto out;
	}

	simple_memset((char*)invocation + sizeof(invocation->object), 0, sizeof(*invocation) - sizeof(invocation->object));

	invocation->function_type = function;
	invocation->channel = channel;

	status = sys_mempool_allocate(name_length, NULL, (void*)&invocation->name);
	if (status != ferr_ok) {
		goto out;
	}

	invocation->name_length = name_length;
	simple_memcpy(invocation->name, name, name_length);

	status = sys_channel_conversation_create(sys_channel, &invocation->conversation_id);
	if (status != ferr_ok) {
		goto out;
	}

	spooky_invocation_function_data_lengths(invocation->function_type, &outgoing_data_length, &incoming_data_length, &invocation->outgoing_callback_info_count, &invocation->incoming_callback_info_count);

	status = sys_mempool_allocate(sizeof(*invocation->incoming_callback_infos) * invocation->incoming_callback_info_count, NULL, (void*)&invocation->incoming_callback_infos);
	if (status != ferr_ok) {
		goto out;
	}

	for (size_t i = 0; i < func->parameter_count; ++i) {
		spooky_function_parameter_info_t* param_info = &func->parameters[i];
		spooky_type_object_t* param_type = (void*)param_info->type;
		spooky_invocation_incoming_callback_info_t* callback_info = &invocation->incoming_callback_infos[callback_info_index];

		if (param_info->direction != spooky_function_parameter_direction_out) {
			continue;
		}

		if (spooky_object_class((void*)param_type) != spooky_object_class_function()) {
			continue;
		}

		callback_info->index = i;
		++callback_info_index;
	}

	callback_info_index = 0;

	status = sys_mempool_allocate(sizeof(*invocation->outgoing_callback_infos) * invocation->outgoing_callback_info_count, NULL, (void*)&invocation->outgoing_callback_infos);
	if (status != ferr_ok) {
		goto out;
	}

	simple_memset(invocation->outgoing_callback_infos, 0, sizeof(*invocation->outgoing_callback_infos) * invocation->outgoing_callback_info_count);

	for (size_t i = 0; i < func->parameter_count; ++i) {
		spooky_function_parameter_info_t* param_info = &func->parameters[i];
		spooky_type_object_t* param_type = (void*)param_info->type;
		spooky_invocation_outgoing_callback_info_t* callback_info = &invocation->outgoing_callback_infos[callback_info_index];

		if (param_info->direction != spooky_function_parameter_direction_in) {
			continue;
		}

		if (spooky_object_class((void*)param_type) != spooky_object_class_function()) {
			continue;
		}

		callback_info->index = i;
		++callback_info_index;

		status = sys_channel_conversation_create(sys_channel, &callback_info->conversation_id);
		if (status != ferr_ok) {
			goto out;
		}
	}

	status = sys_mempool_allocate(incoming_data_length, NULL, (void*)&invocation->incoming_data);
	if (status != ferr_ok) {
		goto out;
	}

	simple_memset(invocation->incoming_data, 0, incoming_data_length);

	status = sys_mempool_allocate(outgoing_data_length, NULL, (void*)&invocation->outgoing_data);
	if (status != ferr_ok) {
		goto out;
	}

	simple_memset(invocation->outgoing_data, 0, outgoing_data_length);

out:
	if (status == ferr_ok) {
		*out_invocation = (void*)invocation;
	} else if (invocation) {
		spooky_release((void*)invocation);
	} else {
		if (function) {
			spooky_release(function);
		}
		if (channel) {
			eve_release(channel);
		}
	}
	return status;
};

ferr_t spooky_invocation_create_incoming(eve_channel_t* channel, sys_channel_message_t* message, spooky_invocation_t** out_invocation) {
	ferr_t status = ferr_ok;
	spooky_invocation_object_t* invocation = NULL;
	spooky_deserializer_t deserializer;
	size_t name_length = 0;
	size_t name_offset = 0;
	spooky_function_object_t* func = NULL;
	size_t incoming_data_length = 0;
	size_t outgoing_data_length = 0;
	size_t callback_info_index = 0;
	sys_channel_t* sys_channel = NULL;
	size_t data_offset = 0;

	if (eve_retain(channel) != ferr_ok) {
		channel = NULL;
		status = ferr_permanent_outage;
		goto out;
	}

	status = eve_channel_target(channel, false, &sys_channel);
	if (status != ferr_ok) {
		goto out;
	}

	status = spooky_deserializer_init(&deserializer, sys_channel_message_data(message), sys_channel_message_length(message));
	if (status != ferr_ok) {
		goto out;
	}

	status = spooky_deserializer_decode_integer(&deserializer, UINT64_MAX, NULL, &name_length, sizeof(name_length), NULL, false);
	if (status != ferr_ok) {
		goto out;
	}

	status = sys_object_new(&invocation_class, sizeof(*invocation) - sizeof(invocation->object), (void*)&invocation);
	if (status != ferr_ok) {
		goto out;
	}

	simple_memset((char*)invocation + sizeof(invocation->object), 0, sizeof(*invocation) - sizeof(invocation->object));

	invocation->channel = channel;
	invocation->incoming = true;
	invocation->conversation_id = sys_channel_message_get_conversation_id(message);

	status = sys_mempool_allocate(name_length, NULL, (void*)&invocation->name);
	if (status != ferr_ok) {
		goto out;
	}

	// this cannot fail
	sys_abort_status(spooky_deserializer_skip(&deserializer, UINT64_MAX, &name_offset, name_length));

	invocation->name_length = name_length;
	simple_memcpy(invocation->name, &deserializer.data[name_offset], name_length);

	status = spooky_deserializer_decode_type(&deserializer, UINT64_MAX, NULL, NULL, &invocation->function_type);
	if (status != ferr_ok) {
		goto out;
	}

	func = (void*)invocation->function_type;

	spooky_invocation_function_data_lengths(invocation->function_type, &incoming_data_length, &outgoing_data_length, &invocation->incoming_callback_info_count, &invocation->outgoing_callback_info_count);

	status = sys_mempool_allocate(incoming_data_length, NULL, (void*)&invocation->incoming_data);
	if (status != ferr_ok) {
		goto out;
	}

	simple_memset(invocation->incoming_data, 0, incoming_data_length);

	status = sys_mempool_allocate(outgoing_data_length, NULL, (void*)&invocation->outgoing_data);
	if (status != ferr_ok) {
		goto out;
	}

	simple_memset(invocation->outgoing_data, 0, outgoing_data_length);

	status = sys_mempool_allocate(sizeof(*invocation->incoming_callback_infos) * invocation->incoming_callback_info_count, NULL, (void*)&invocation->incoming_callback_infos);
	if (status != ferr_ok) {
		goto out;
	}

	for (size_t i = 0; i < func->parameter_count; ++i) {
		spooky_function_parameter_info_t* param_info = &func->parameters[i];
		spooky_type_object_t* param_type = (void*)param_info->type;
		spooky_invocation_incoming_callback_info_t* callback_info = &invocation->incoming_callback_infos[callback_info_index];

		if (param_info->direction != spooky_function_parameter_direction_in) {
			continue;
		}

		if (spooky_object_class((void*)param_type) != spooky_object_class_function()) {
			continue;
		}

		callback_info->index = i;
		++callback_info_index;
	}

	callback_info_index = 0;

	status = sys_mempool_allocate(sizeof(*invocation->outgoing_callback_infos) * invocation->outgoing_callback_info_count, NULL, (void*)&invocation->outgoing_callback_infos);
	if (status != ferr_ok) {
		goto out;
	}

	simple_memset(invocation->outgoing_callback_infos, 0, sizeof(*invocation->outgoing_callback_infos) * invocation->outgoing_callback_info_count);

	for (size_t i = 0; i < func->parameter_count; ++i) {
		spooky_function_parameter_info_t* param_info = &func->parameters[i];
		spooky_type_object_t* param_type = (void*)param_info->type;
		spooky_invocation_outgoing_callback_info_t* callback_info = &invocation->outgoing_callback_infos[callback_info_index];

		if (param_info->direction != spooky_function_parameter_direction_out) {
			continue;
		}

		if (spooky_object_class((void*)param_type) != spooky_object_class_function()) {
			continue;
		}

		callback_info->index = i;
		++callback_info_index;

		status = sys_channel_conversation_create(sys_channel, &callback_info->conversation_id);
		if (status != ferr_ok) {
			goto out;
		}
	}

	for (size_t i = 0; i < func->parameter_count; ++i) {
		spooky_function_parameter_info_t* param_info = &func->parameters[i];

		if (param_info->direction == spooky_function_parameter_direction_out) {
			continue;
		}

		status = spooky_invocation_deserialize_object(invocation, &deserializer, &invocation->incoming_data[data_offset], param_info->type, i);
		if (status != ferr_ok) {
			goto out;
		}

		data_offset += ((spooky_type_object_t*)param_info->type)->byte_size;
	}

out:
	if (status == ferr_ok) {
		*out_invocation = (void*)invocation;
	} else if (invocation) {
		spooky_release((void*)invocation);
	} else {
		if (channel) {
			eve_release(channel);
		}
	}
	return status;
};

LIBSPOOKY_STRUCT(spooky_invocation_callback_context) {
	spooky_function_implementation_f implementation;
	void* context;
};

static void spooky_invocation_callback(void* _context, eve_channel_t* channel, sys_channel_message_t* message, ferr_t status) {
	spooky_invocation_callback_context_t* context = _context;
	spooky_invocation_t* incoming_invocation = NULL;

	if (status == ferr_permanent_outage) {
		// should be no message here
		if (message) {
			sys_abort();
		}

		// invoke the user handler with nothing (so it can clean up its context)
	} else if (status == ferr_cancelled) {
		// just clean up our context but don't invoke the user handler;
		// this only occurs when we are still setting up the outgoing message in spooky_invocation_execute_async()
		goto out;
	} else if (status == ferr_ok) {
		status = spooky_invocation_create_incoming(channel, message, &incoming_invocation);
		if (status != ferr_ok) {
			// discard the message and invoke the user handler with nothing
			sys_release(message);
		}
	} else if (message) {
		// this should be impossible
		// (we should only get a message if status == ferr_ok),
		// but just in case, release the message and invoke the user handler with nothing
		sys_release(message);
	}

	context->implementation(context->context, incoming_invocation);

out:
	LIBSPOOKY_WUR_IGNORE(sys_mempool_free(context));
};

LIBSPOOKY_STRUCT(spooky_invocation_execute_async_context) {
	spooky_invocation_object_t* invocation;
	spooky_invocation_complete_f completion_callback;
	void* context;
};

static void spooky_invocation_execute_async_reply_handler(void* _context, eve_channel_t* channel, sys_channel_message_t* message, ferr_t status) {
	spooky_invocation_execute_async_context_t* context = _context;

	if (status == ferr_permanent_outage) {
		// should be no message here
		if (message) {
			sys_abort();
		}

		// no need to clean up callback listeners; they'll also receive "ferr_permanent_outage"
	} else if (status == ferr_ok) {
		spooky_deserializer_t deserializer;
		size_t data_offset = 0;
		spooky_function_object_t* func = (void*)context->invocation->function_type;

		status = spooky_deserializer_init(&deserializer, sys_channel_message_data(message), sys_channel_message_length(message));
		if (status != ferr_ok) {
			goto cleanup;
		}

		// TODO: have the peer send back the function type they're using and check it matches ours

		for (size_t i = 0; i < func->parameter_count; ++i) {
			spooky_function_parameter_info_t* param_info = &func->parameters[i];

			if (param_info->direction == spooky_function_parameter_direction_in) {
				continue;
			}

			status = spooky_invocation_deserialize_object(context->invocation, &deserializer, &context->invocation->incoming_data[data_offset], param_info->type, i);
			if (status != ferr_ok) {
				goto cleanup;
			}

			data_offset += ((spooky_type_object_t*)param_info->type)->byte_size;
		}

cleanup:
		sys_release(message);
	} else if (message) {
		// this is the message we were trying to send; just release it and report the error back to the user
		sys_release(message);
	}

	context->completion_callback(context->context, (void*)context->invocation, status);

	spooky_release((void*)context->invocation);
	LIBSPOOKY_WUR_IGNORE(sys_mempool_free(context));
};

static ferr_t spooky_invocation_serialize_contents(spooky_invocation_object_t* invocation, spooky_serializer_t* serializer, size_t* out_outgoing_callback_index, sys_channel_message_t** out_message) {
	spooky_function_object_t* func = (void*)invocation->function_type;
	ferr_t status = ferr_ok;
	size_t data_offset = 0;
	size_t outgoing_callback_index = 0;

	for (size_t i = 0; i < func->parameter_count; ++i) {
		spooky_function_parameter_info_t* param_info = &func->parameters[i];

		if (param_info->direction == (invocation->incoming ? spooky_function_parameter_direction_in : spooky_function_parameter_direction_out)) {
			continue;
		}

		status = spooky_invocation_serialize_object(invocation, serializer, &invocation->outgoing_data[data_offset], param_info->type, i);
		if (status != ferr_ok) {
			goto out;
		}

		data_offset += ((spooky_type_object_t*)param_info->type)->byte_size;
	}

	for (; outgoing_callback_index < invocation->outgoing_callback_info_count; ++outgoing_callback_index) {
		spooky_invocation_outgoing_callback_info_t* callback_info = &invocation->outgoing_callback_infos[outgoing_callback_index];
		spooky_invocation_callback_context_t* callback_context = NULL;

		status = sys_mempool_allocate(sizeof(*callback_context), NULL, (void*)&callback_context);
		if (status != ferr_ok) {
			goto out;
		}

		callback_context->implementation = callback_info->implementation;
		callback_context->context = callback_info->context;

		status = eve_channel_receive_conversation_async(invocation->channel, callback_info->conversation_id, spooky_invocation_callback, callback_context, &callback_info->cancellation_token);
		if (status != ferr_ok) {
			LIBSPOOKY_WUR_IGNORE(sys_mempool_free(callback_context));
			goto out;
		}
	}

	// TODO: modify the serializer class to create a message itself
	status = sys_channel_message_create_copy(serializer->data, serializer->length, out_message);
	if (status != ferr_ok) {
		goto out;
	}

	LIBSPOOKY_WUR_IGNORE(sys_mempool_free(serializer->data));
	serializer->data = NULL;

out:
	*out_outgoing_callback_index = outgoing_callback_index;
	return status;
};

ferr_t spooky_invocation_execute_async(spooky_invocation_t* obj, spooky_invocation_complete_f completion_callback, void* context) {
	ferr_t status = ferr_ok;
	spooky_invocation_object_t* invocation = (void*)obj;
	spooky_serializer_t serializer;
	size_t outgoing_callback_index = 0;
	spooky_function_object_t* func = (void*)invocation->function_type;
	size_t data_offset = 0;
	size_t name_offset = UINT64_MAX;
	sys_channel_message_t* message = NULL;
	spooky_invocation_execute_async_context_t* reply_context = NULL;

	// this cannot fail
	sys_abort_status(spooky_retain((void*)invocation));
	sys_abort_status(spooky_retain((void*)invocation));

	if (invocation->incoming) {
		status = ferr_invalid_argument;
		goto out;
	}

	status = spooky_serializer_init(&serializer);
	if (status != ferr_ok) {
		goto out;
	}

	status = sys_mempool_allocate(sizeof(*reply_context), NULL, (void*)&reply_context);
	if (status != ferr_ok) {
		goto out;
	}

	reply_context->completion_callback = completion_callback;
	reply_context->context = context;
	reply_context->invocation = invocation;

	status = spooky_serializer_encode_integer(&serializer, UINT64_MAX, NULL, &invocation->name_length, sizeof(invocation->name_length), NULL, false);
	if (status != ferr_ok) {
		return status;
	}

	status = spooky_serializer_reserve(&serializer, name_offset, &name_offset, invocation->name_length);
	if (status != ferr_ok) {
		goto out;
	}

	simple_memcpy(&serializer.data[name_offset], invocation->name, invocation->name_length);

	status = spooky_serializer_encode_type(&serializer, UINT64_MAX, NULL, NULL, invocation->function_type);
	if (status != ferr_ok) {
		goto out;
	}

	status = spooky_invocation_serialize_contents(invocation, &serializer, &outgoing_callback_index, &message);
	if (status != ferr_ok) {
		goto out;
	}

	sys_channel_message_set_conversation_id(message, invocation->conversation_id);

	status = eve_channel_send_with_reply_async(invocation->channel, message, spooky_invocation_execute_async_reply_handler, reply_context);
	if (status != ferr_ok) {
		goto out;
	}

	// consumed by the send
	message = NULL;

	// outgoing callbacks are also consumed by the send
	for (size_t i = 0; i < invocation->outgoing_callback_info_count; ++i) {
		invocation->outgoing_callback_infos[i].implementation = NULL;
	}

out:
	if (message) {
		sys_release(message);
	}

	if (serializer.data) {
		LIBSPOOKY_WUR_IGNORE(sys_mempool_free(serializer.data));
	}

	if (status != ferr_ok) {
		for (size_t i = 0; i < outgoing_callback_index; ++i) {
			spooky_invocation_outgoing_callback_info_t* callback_info = &invocation->outgoing_callback_infos[i];
			LIBSPOOKY_WUR_IGNORE(eve_channel_receive_conversation_cancel(invocation->channel, callback_info->conversation_id, callback_info->cancellation_token));
		}
		if (reply_context) {
			LIBSPOOKY_WUR_IGNORE(sys_mempool_free(reply_context));
		}
		spooky_release((void*)invocation);
	}
	spooky_release((void*)invocation);
	return status;
};

LIBSPOOKY_STRUCT(spooky_invocation_execute_sync_context) {
	ferr_t status;
	sys_semaphore_t semaphore;
};

static void spooky_invocation_execute_sync_complete(void* _context, spooky_invocation_t* invocation, ferr_t status) {
	spooky_invocation_execute_sync_context_t* context = _context;
	context->status = status;
	sys_semaphore_up(&context->semaphore);
};

ferr_t spooky_invocation_execute_sync(spooky_invocation_t* obj) {
	spooky_invocation_object_t* invocation = (void*)obj;
	spooky_invocation_execute_sync_context_t context;

	context.status = ferr_ok;
	sys_semaphore_init(&context.semaphore, 0);

	context.status = spooky_invocation_execute_async(obj, spooky_invocation_execute_sync_complete, &context);
	if (context.status != ferr_ok) {
		goto out;
	}

	// this might be a long wait, so use the suspendable version
	eve_semaphore_down(&context.semaphore);

out:
	return context.status;
};

ferr_t spooky_invocation_complete(spooky_invocation_t* obj) {
	ferr_t status = ferr_ok;
	spooky_invocation_object_t* invocation = (void*)obj;
	spooky_serializer_t serializer;
	size_t outgoing_callback_index = 0;
	size_t data_offset = 0;
	spooky_function_object_t* func = (void*)invocation->function_type;
	sys_channel_message_t* message = NULL;

	if (!invocation->incoming) {
		status = ferr_invalid_argument;
		goto out;
	}

	status = spooky_serializer_init(&serializer);
	if (status != ferr_ok) {
		goto out;
	}

	status = spooky_invocation_serialize_contents(invocation, &serializer, &outgoing_callback_index, &message);
	if (status != ferr_ok) {
		goto out;
	}

	sys_channel_message_set_conversation_id(message, invocation->conversation_id);

	// TODO: add an option to have a custom send error handler for a single message in libeve.
	//       this would allow us to send the message asynchronously here.
	//       this isn't too bad, though, since libeve suspends the current work item for the wait.
	status = eve_channel_send(invocation->channel, message, true);
	if (status != ferr_ok) {
		goto out;
	}

	// consumed by the send
	message = NULL;

	// outgoing callbacks are also consumed by the send
	for (size_t i = 0; i < invocation->outgoing_callback_info_count; ++i) {
		invocation->outgoing_callback_infos[i].implementation = NULL;
	}

out:
	if (message) {
		sys_release(message);
	}

	if (serializer.data) {
		LIBSPOOKY_WUR_IGNORE(sys_mempool_free(serializer.data));
	}

	if (status != ferr_ok) {
		for (size_t i = 0; i < outgoing_callback_index; ++i) {
			spooky_invocation_outgoing_callback_info_t* callback_info = &invocation->outgoing_callback_infos[i];
			LIBSPOOKY_WUR_IGNORE(eve_channel_receive_conversation_cancel(invocation->channel, callback_info->conversation_id, callback_info->cancellation_token));
		}
		sys_release((void*)invocation);
	}

	return status;
};

bool spooky_invocation_is_incoming(spooky_invocation_t* obj) {
	spooky_invocation_object_t* invocation = (void*)obj;
	return invocation->incoming;
};

#define SPOOKY_INVOCATION_BASIC_ACCESSOR_DEF(_type, _typecode) \
	ferr_t spooky_invocation_get_ ## _typecode(spooky_invocation_t* obj, size_t index, _type* out_value) { \
		ferr_t status = ferr_ok; \
		spooky_invocation_object_t* invocation = (void*)obj; \
		spooky_function_object_t* func = (void*)invocation->function_type; \
		bool is_incoming = false; \
		if (index >= func->parameter_count) { \
			status = ferr_invalid_argument; \
			goto out; \
		} \
		if (func->parameters[index].type != spooky_type_ ## _typecode()) { \
			status = ferr_invalid_argument; \
			goto out; \
		} \
		if (invocation->incoming) { \
			is_incoming = func->parameters[index].direction == spooky_function_parameter_direction_in; \
		} else { \
			is_incoming = func->parameters[index].direction == spooky_function_parameter_direction_out; \
			if (is_incoming && !invocation->incoming_data) { \
				status = ferr_resource_unavailable; \
				goto out; \
			} \
		} \
		simple_memcpy(out_value, &(is_incoming ? invocation->incoming_data : invocation->outgoing_data)[func->parameters[index].offset], sizeof(*out_value)); \
	out: \
		return status; \
	}; \
	ferr_t spooky_invocation_set_ ## _typecode(spooky_invocation_t* obj, size_t index, _type value) { \
		ferr_t status = ferr_ok; \
		spooky_invocation_object_t* invocation = (void*)obj; \
		spooky_function_object_t* func = (void*)invocation->function_type; \
		bool is_incoming = false; \
		if (index >= func->parameter_count) { \
			status = ferr_invalid_argument; \
			goto out; \
		} \
		if (func->parameters[index].type != spooky_type_ ## _typecode()) { \
			status = ferr_invalid_argument; \
			goto out; \
		} \
		if (invocation->incoming) { \
			is_incoming = func->parameters[index].direction == spooky_function_parameter_direction_in; \
		} else { \
			is_incoming = func->parameters[index].direction == spooky_function_parameter_direction_out; \
			if (is_incoming && !invocation->incoming_data) { \
				status = ferr_resource_unavailable; \
				goto out; \
			} \
		} \
		simple_memcpy(&(is_incoming ? invocation->incoming_data : invocation->outgoing_data)[func->parameters[index].offset], &value, sizeof(value)); \
	out: \
		return status; \
	};

SPOOKY_INVOCATION_BASIC_ACCESSOR_DEF(uint8_t, u8);
SPOOKY_INVOCATION_BASIC_ACCESSOR_DEF(uint16_t, u16);
SPOOKY_INVOCATION_BASIC_ACCESSOR_DEF(uint32_t, u32);
SPOOKY_INVOCATION_BASIC_ACCESSOR_DEF(uint64_t, u64);
SPOOKY_INVOCATION_BASIC_ACCESSOR_DEF(int8_t, i8);
SPOOKY_INVOCATION_BASIC_ACCESSOR_DEF(int16_t, i16);
SPOOKY_INVOCATION_BASIC_ACCESSOR_DEF(int32_t, i32);
SPOOKY_INVOCATION_BASIC_ACCESSOR_DEF(int64_t, i64);

SPOOKY_INVOCATION_BASIC_ACCESSOR_DEF(float, f32);
SPOOKY_INVOCATION_BASIC_ACCESSOR_DEF(double, f64);

SPOOKY_INVOCATION_BASIC_ACCESSOR_DEF(bool, bool);

ferr_t spooky_invocation_get_data(spooky_invocation_t* obj, size_t index, bool retain, spooky_data_t** out_data) {
	ferr_t status = ferr_ok;
	spooky_invocation_object_t* invocation = (void*)obj;
	spooky_function_object_t* func = (void*)invocation->function_type;
	bool is_incoming = false;
	spooky_data_t* data = NULL;

	if (index >= func->parameter_count) {
		status = ferr_invalid_argument;
		goto out;
	}

	if (func->parameters[index].type != spooky_type_data()) {
		status = ferr_invalid_argument;
		goto out;
	}

	if (invocation->incoming) {
		is_incoming = func->parameters[index].direction == spooky_function_parameter_direction_in;
	} else {
		is_incoming = func->parameters[index].direction == spooky_function_parameter_direction_out;
		if (is_incoming && !invocation->incoming_data) {
			status = ferr_resource_unavailable;
			goto out;
		}
	}

	data = *(spooky_data_t**)(&(is_incoming ? invocation->incoming_data : invocation->outgoing_data)[func->parameters[index].offset]);

	if (retain) {
		status = spooky_retain(data);
		if (status != ferr_ok) {
			goto out;
		}
	}

	*out_data = data;

out:
	return status;
};

ferr_t spooky_invocation_set_data(spooky_invocation_t* obj, size_t index, spooky_data_t* data) {
	ferr_t status = ferr_ok;
	spooky_invocation_object_t* invocation = (void*)obj;
	spooky_function_object_t* func = (void*)invocation->function_type;
	bool is_incoming = false;
	spooky_data_t** data_ptr = NULL;
	spooky_data_t* old_data = NULL;

	if (index >= func->parameter_count) {
		status = ferr_invalid_argument;
		goto out;
	}

	if (func->parameters[index].type != spooky_type_data()) {
		status = ferr_invalid_argument;
		goto out;
	}

	if (invocation->incoming) {
		is_incoming = func->parameters[index].direction == spooky_function_parameter_direction_in;
	} else {
		is_incoming = func->parameters[index].direction == spooky_function_parameter_direction_out;
		if (is_incoming && !invocation->incoming_data) {
			status = ferr_resource_unavailable;
			goto out;
		}
	}

	if (spooky_retain(data) != ferr_ok) {
		status = ferr_permanent_outage;
		goto out;
	}

	data_ptr = (spooky_data_t**)(&(is_incoming ? invocation->incoming_data : invocation->outgoing_data)[func->parameters[index].offset]);
	old_data = *data_ptr;
	*data_ptr = data;

	if (old_data) {
		spooky_release(old_data);
	}

out:
	return status;
};

ferr_t spooky_invocation_get_structure(spooky_invocation_t* obj, size_t index, bool retain_members, void* out_structure, size_t* in_out_structure_size) {
	ferr_t status = ferr_ok;
	spooky_invocation_object_t* invocation = (void*)obj;
	spooky_function_object_t* func = (void*)invocation->function_type;
	bool is_incoming = false;
	spooky_structure_object_t* structure = NULL;
	size_t input_size = *in_out_structure_size;
	void* struct_base = NULL;

	if (index >= func->parameter_count) {
		status = ferr_invalid_argument;
		goto out;
	}

	if (spooky_object_class(func->parameters[index].type) != spooky_object_class_structure()) {
		status = ferr_invalid_argument;
		goto out;
	}

	structure = (void*)func->parameters[index].type;
	// always set the structure size so that callers know how much space they need
	*in_out_structure_size = structure->base.byte_size;

	if (structure->base.byte_size > input_size) {
		status = ferr_too_big;
		goto out;
	}

	if (invocation->incoming) {
		is_incoming = func->parameters[index].direction == spooky_function_parameter_direction_in;
	} else {
		is_incoming = func->parameters[index].direction == spooky_function_parameter_direction_out;
		if (is_incoming && !invocation->incoming_data) {
			status = ferr_resource_unavailable;
			goto out;
		}
	}

	struct_base = &(is_incoming ? invocation->incoming_data : invocation->outgoing_data)[func->parameters[index].offset];

	if (retain_members) {
		status = spooky_invocation_retain_object(struct_base, (void*)structure);
		if (status != ferr_ok) {
			goto out;
		}
	}

	simple_memcpy(out_structure, struct_base, structure->base.byte_size);

out:
	return status;
};

ferr_t spooky_invocation_set_structure(spooky_invocation_t* obj, size_t index, const void* structure) {
	ferr_t status = ferr_ok;
	spooky_invocation_object_t* invocation = (void*)obj;
	spooky_function_object_t* func = (void*)invocation->function_type;
	bool is_incoming = false;
	void* struct_base = NULL;
	spooky_structure_object_t* structure_type = NULL;

	if (index >= func->parameter_count) {
		status = ferr_invalid_argument;
		goto out;
	}

	if (spooky_object_class(func->parameters[index].type) != spooky_object_class_structure()) {
		status = ferr_invalid_argument;
		goto out;
	}

	structure_type = (void*)func->parameters[index].type;

	if (invocation->incoming) {
		is_incoming = func->parameters[index].direction == spooky_function_parameter_direction_in;
	} else {
		is_incoming = func->parameters[index].direction == spooky_function_parameter_direction_out;
		if (is_incoming && !invocation->incoming_data) {
			status = ferr_resource_unavailable;
			goto out;
		}
	}

	struct_base = &(is_incoming ? invocation->incoming_data : invocation->outgoing_data)[func->parameters[index].offset];

	status = spooky_invocation_retain_object(structure, (void*)structure_type);
	if (status != ferr_ok) {
		goto out;
	}

	spooky_invocation_release_object(struct_base, (void*)structure_type);

	simple_memcpy(struct_base, structure, structure_type->base.byte_size);

out:
	return status;
};

ferr_t spooky_invocation_get_function(spooky_invocation_t* obj, size_t index, spooky_function_implementation_f* out_function, void** out_context) {
	ferr_t status = ferr_ok;
	spooky_invocation_object_t* invocation = (void*)obj;
	spooky_function_object_t* func = (void*)invocation->function_type;

	if (index >= func->parameter_count) {
		status = ferr_invalid_argument;
		goto out;
	}

	if (spooky_object_class(func->parameters[index].type) != spooky_object_class_function()) {
		status = ferr_invalid_argument;
		goto out;
	}

	if (func->parameters[index].direction == spooky_function_parameter_direction_in) {
		if (invocation->incoming) {
			status = ferr_invalid_argument;
			goto out;
		}
	} else {
		if (!invocation->incoming) {
			status = ferr_invalid_argument;
			goto out;
		}
	}

	status = ferr_invalid_argument;

	for (size_t i = 0; i < invocation->outgoing_callback_info_count; ++i) {
		spooky_invocation_outgoing_callback_info_t* callback_info = &invocation->outgoing_callback_infos[i];

		if (callback_info->index == index) {
			status = ferr_ok;
			*out_function = callback_info->implementation;
			*out_context = callback_info->context;
			goto out;
		}
	}

out:
	return status;
};

ferr_t spooky_invocation_set_function(spooky_invocation_t* obj, size_t index, spooky_function_implementation_f function, void* context) {
	ferr_t status = ferr_ok;
	spooky_invocation_object_t* invocation = (void*)obj;
	spooky_function_object_t* func = (void*)invocation->function_type;

	if (index >= func->parameter_count) {
		status = ferr_invalid_argument;
		goto out;
	}

	if (spooky_object_class(func->parameters[index].type) != spooky_object_class_function()) {
		status = ferr_invalid_argument;
		goto out;
	}

	if (func->parameters[index].direction == spooky_function_parameter_direction_in) {
		if (invocation->incoming) {
			status = ferr_invalid_argument;
			goto out;
		}
	} else {
		if (!invocation->incoming) {
			status = ferr_invalid_argument;
			goto out;
		}
	}

	status = ferr_invalid_argument;

	for (size_t i = 0; i < invocation->outgoing_callback_info_count; ++i) {
		spooky_invocation_outgoing_callback_info_t* callback_info = &invocation->outgoing_callback_infos[i];

		if (callback_info->index == index) {
			status = ferr_ok;
			callback_info->implementation = function;
			callback_info->context = context;
			goto out;
		}
	}

out:
	return status;
};

ferr_t spooky_invocation_get_invocation(spooky_invocation_t* obj, size_t index, spooky_invocation_t** out_invocation) {
	ferr_t status = ferr_ok;
	spooky_invocation_object_t* invocation = (void*)obj;
	spooky_invocation_object_t* new_invocation = NULL;
	spooky_function_object_t* func = (void*)invocation->function_type;

	if (index >= func->parameter_count) {
		status = ferr_invalid_argument;
		goto out;
	}

	if (spooky_object_class(func->parameters[index].type) != spooky_object_class_function()) {
		status = ferr_invalid_argument;
		goto out;
	}

	if (func->parameters[index].direction == spooky_function_parameter_direction_in) {
		if (!invocation->incoming) {
			status = ferr_invalid_argument;
			goto out;
		}
	} else {
		if (invocation->incoming) {
			status = ferr_invalid_argument;
			goto out;
		}
	}

	status = ferr_invalid_argument;

	for (size_t i = 0; i < invocation->incoming_callback_info_count; ++i) {
		spooky_invocation_incoming_callback_info_t* callback_info = &invocation->incoming_callback_infos[i];

		if (callback_info->index == index) {
			if (callback_info->conversation_id == sys_channel_conversation_id_none) {
				status = invocation->incoming ? ferr_permanent_outage : ferr_resource_unavailable;
				goto out;
			}

			status = spooky_invocation_create(NULL, 0, func->parameters[index].type, invocation->channel, (void*)&new_invocation);
			if (status != ferr_ok) {
				goto out;
			}

			new_invocation->conversation_id = callback_info->conversation_id;
			callback_info->conversation_id = sys_channel_conversation_id_none;

			goto out;
		}
	}

out:
	if (status == ferr_ok) {
		*out_invocation = (void*)new_invocation;
	} else if (new_invocation) {
		sys_release((void*)new_invocation);
	}
	return status;
};