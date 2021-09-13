#include <ferro/gdbstub/packet-buffer.h>
#include <ferro/core/mempool.h>
#include <ferro/core/panic.h>

#include <libk/libk.h>

ferr_t fgdb_packet_buffer_init(fgdb_packet_buffer_t* packet_buffer, uint8_t* static_buffer, size_t static_buffer_size) {
	packet_buffer->mempooled = false;
	packet_buffer->buffer = static_buffer;
	packet_buffer->length = 0;
	packet_buffer->size = static_buffer_size;
	packet_buffer->offset = 0;
	return ferr_ok;
};

void fgdb_packet_buffer_destroy(fgdb_packet_buffer_t* packet_buffer) {
	if (packet_buffer->mempooled) {
		if (fmempool_free(packet_buffer->buffer) != ferr_ok) {
			fpanic("Failed to free packet buffer memory");
		}
	}
};

ferr_t fgdb_packet_buffer_grow(fgdb_packet_buffer_t* packet_buffer) {
	if (packet_buffer->mempooled) {
		if (fmempool_reallocate(packet_buffer->buffer, packet_buffer->size * 2, &packet_buffer->size, (void*)&packet_buffer->buffer) != ferr_ok) {
			return ferr_temporary_outage;
		}
	} else {
		uint8_t* new_buffer;
		if (fmempool_allocate(packet_buffer->size * 2, &packet_buffer->size, (void*)&new_buffer) != ferr_ok) {
			return ferr_temporary_outage;
		}
		memcpy(new_buffer, packet_buffer->buffer, packet_buffer->length);
		packet_buffer->buffer = new_buffer;
		packet_buffer->mempooled = true;
	}
	return ferr_ok;
};

ferr_t fgdb_packet_buffer_append(fgdb_packet_buffer_t* packet_buffer, const uint8_t* data, size_t length) {
	while (packet_buffer->size - packet_buffer->length < length) {
		if (fgdb_packet_buffer_grow(packet_buffer) != ferr_ok) {
			return ferr_temporary_outage;
		}
	}

	memcpy(&packet_buffer->buffer[packet_buffer->length], data, length);
	packet_buffer->length += length;

	return ferr_ok;
};

static uint8_t from_hex_digit(char digit) {
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

#define FGDB_PACKET_BUFFER_SERIALIZE_GENERIC(_type, _suffix, _byte_count) \
	ferr_t fgdb_packet_buffer_serialize_ ## _suffix(fgdb_packet_buffer_t* packet_buffer, _type value, bool big_endian) { \
		ferr_t status = ferr_ok; \
		for (size_t i = (big_endian) ? _byte_count : 1; (big_endian) ? (i > 0) : (i <= _byte_count); (big_endian) ? (--i) : (++i)) { \
			uint8_t byte = (value >> ((i - 1) * 8)) & 0xff; \
			char hex[2]; \
			hex[0] = to_hex_digit(byte >> 4); \
			hex[1] = to_hex_digit(byte & 0x0f); \
			status = fgdb_packet_buffer_append(packet_buffer, (const uint8_t*)&hex[0], sizeof(hex)); \
			if (status != ferr_ok) { \
				return status; \
			} \
		} \
		return status; \
	};

FGDB_PACKET_BUFFER_SERIALIZE_GENERIC(uint64_t, u64, 8);
FGDB_PACKET_BUFFER_SERIALIZE_GENERIC(uint32_t, u32, 4);
FGDB_PACKET_BUFFER_SERIALIZE_GENERIC(uint16_t, u16, 2);
FGDB_PACKET_BUFFER_SERIALIZE_GENERIC(uint8_t, u8, 1);

#define FGDB_PACKET_BUFFER_DESERIALIZE_GENERIC(_type, _suffix, _byte_count) \
	ferr_t fgdb_packet_buffer_deserialize_ ## _suffix(fgdb_packet_buffer_t* packet_buffer, bool big_endian, _type* out_value) { \
		ferr_t status = ferr_invalid_argument; \
		_type result = 0; \
		for (size_t i = 0; i < _byte_count; ++i) { \
			if (packet_buffer->offset + 1 >= packet_buffer->length) { \
				break; \
			} \
			uint8_t high = from_hex_digit(packet_buffer->buffer[packet_buffer->offset]); \
			uint8_t low = from_hex_digit(packet_buffer->buffer[packet_buffer->offset + 1]); \
			uint8_t value = (high << 4) | low; \
			if (high == UINT8_MAX || low == UINT8_MAX) { \
				break; \
			} \
			packet_buffer->offset += 2; \
			if (big_endian) { \
				result = (result << 8) | value; \
			} else { \
				result = ((_type)value << (i * 8)) | result; \
			} \
			status = ferr_ok; \
		} \
		if (status == ferr_ok && out_value) { \
			*out_value = result; \
		} \
		return status; \
	};

FGDB_PACKET_BUFFER_DESERIALIZE_GENERIC(uint64_t, u64, 8);
FGDB_PACKET_BUFFER_DESERIALIZE_GENERIC(uint32_t, u32, 4);
FGDB_PACKET_BUFFER_DESERIALIZE_GENERIC(uint16_t, u16, 2);
FGDB_PACKET_BUFFER_DESERIALIZE_GENERIC(uint8_t, u8, 1);

ferr_t fgdb_packet_buffer_serialize_data(fgdb_packet_buffer_t* packet_buffer, const uint8_t* data, size_t length) {
	ferr_t status = ferr_ok;

	for (size_t i = 0; i < length; ++i) {
		uint8_t byte = data[i];

		switch (byte) {
			case '#':
			case '$':
			case '}':
			case '*':
				byte ^= 0x20;
				status = fgdb_packet_buffer_append(packet_buffer, (const uint8_t*)"}", 1);
				if (status != ferr_ok) {
					return status;
				}
				break;
		}

		status = fgdb_packet_buffer_append(packet_buffer, &byte, 1);
		if (status != ferr_ok) {
			return status;
		}
	}

	return status;
};
