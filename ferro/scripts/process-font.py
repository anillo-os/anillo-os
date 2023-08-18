#!/usr/bin/env python3

import os
import struct
import errno
import sys

SCRIPT_DIR = os.path.dirname(__file__)
SOURCE_ROOT = os.path.join(SCRIPT_DIR, '..', '..')

sys.path.append(os.path.join(SOURCE_ROOT, 'scripts'))
import anillo_util

if len(sys.argv) != 3:
	print('Usage: ' + sys.argv[0] + ' <input-font> <output-dir>')
	sys.exit(1)

FONT_PATH = sys.argv[1]
OUTPUT_PATH = sys.argv[2]

PSF1_MAGIC = bytearray([0x36, 0x04])
PSF2_MAGIC = bytearray([0x72, 0xb5, 0x4a, 0x86])
PSF2_UNICODE_FLAG = 0x01

PSF1_512_FLAG = 0x01
PSF1_UNICODE_FLAG = 0x02

contents = []
with open(FONT_PATH, 'rb') as file:
	contents = bytearray(file.read())

magic = bytearray(contents[0:2])

if magic != PSF1_MAGIC:
	magic = bytearray(contents[0:4])
	if magic != PSF2_MAGIC:
		print('Invalid PSF magic')
		exit(1)

version = 0
header_size = 0
flags = 0
glyph_count = 0
glyph_size = 0
glyph_height = 0
glyph_width = 0

if magic == PSF1_MAGIC:
	header = struct.unpack('BB', contents[2:4])
	header_size = 4
	flags = header[0]
	glyph_count = 512 if (flags & PSF1_512_FLAG) != 0 else 256
	glyph_size = header[1]
	glyph_width = 8
	glyph_height = glyph_size
elif magic == PSF2_MAGIC:
	header = struct.unpack('IIIIIII', contents[4:32])
	version = header[0]
	header_size = header[1]
	flags = header[2]
	glyph_count = header[3]
	glyph_size = header[4]
	glyph_height = header[5]
	glyph_width = header[6]
else:
	assert False

table_offset = header_size + (glyph_size * glyph_count)
unicode_map = [0xffff] * 0xffff
font_data = contents[header_size:table_offset]

if magic == PSF1_MAGIC:
	# Ferro expects a PSF2 font; let's convert the header to a PSF2 header
	font_header = bytearray(struct.pack('4BIIIIIII', PSF2_MAGIC[0], PSF2_MAGIC[1], PSF2_MAGIC[2], PSF2_MAGIC[3], version, struct.calcsize('4BIIIIIII'), PSF2_UNICODE_FLAG if (flags & PSF1_UNICODE_FLAG) != 0 else 0, glyph_count, glyph_size, glyph_height, glyph_width))
else:
	font_header = contents[0:32]

if magic == PSF2_MAGIC and (flags & PSF2_UNICODE_FLAG) != 0:
	table = contents[table_offset:]

	# general process derived from https://wiki.osdev.org/PC_Screen_Font

	table_index = 0
	glyph_index = 0

	def read_utf8():
		curr = table[table_index]

		if curr & 0x80:
			if (curr & 0x20) == 0:
				curr = (((table[table_index] & 0x1f) << 6) | (table[table_index + 1] & 0x3f)) & 0xffff
				table_index += 1
			elif (curr & 0x10) == 0:
				curr = (((table[table_index] & 0x0f) << 12) | ((table[table_index + 1] & 0x3f) << 6) | (table[table_index + 2] & 0x3f)) & 0xffff
				table_index += 2
			elif (curr & 0x08) == 0:
				curr = (((table[table_index] & 0x07) << 18) | ((table[table_index + 1] & 0x3f) << 12) | ((table[table_index + 2] & 0x3f) << 6) | (table[table_index + 3] & 0x3f)) & 0xffff
				table_index += 3
			else:
				curr = 0

		table_index += 1

		return curr

	while table_index < len(table):
		curr = table[table_index]

		if curr == 0xff:
			# handle the terminator
			table_index += 1
			glyph_index += 1
		elif curr == 0xfe:
			# skip combining symbols
			while curr != 0xff:
				if curr == 0xfe:
					table_index += 1
					curr = table[table_index]
				else:
					read_utf8()
					curr = table[table_index]

			# now handle the terminator
			table_index += 1
			glyph_index += 1
		else:
			curr = read_utf8()
			unicode_map[curr] = glyph_index
elif magic == PSF1_MAGIC and (flags & PSF1_UNICODE_FLAG) != 0:
	table = contents[table_offset:]

	table_index = 0
	glyph_index = 0

	while table_index < len(table):
		curr = struct.unpack_from('<H', table, table_index)[0]

		if curr == 0xffff:
			# handle the terminator
			table_index += 2
			glyph_index += 1
		elif curr == 0xfffe:
			# skip combining symbols
			while curr != 0xffff:
				table_index += 2
				curr = struct.unpack_from('<H', table, table_index)[0]

			# now handle the terminator
			table_index += 2
			glyph_index += 1
		else:
			unicode_map[curr] = glyph_index
			table_index += 2
else:
	unicode_map = []

anillo_util.mkdir_p(OUTPUT_PATH)

with open(os.path.join(OUTPUT_PATH, 'header.bin'), 'wb') as outfile:
	outfile.write(font_header)

with open(os.path.join(OUTPUT_PATH, 'data.bin'), 'wb') as outfile:
	outfile.write(font_data)

with open(os.path.join(OUTPUT_PATH, 'unicode.bin'), 'wb') as outfile:
	unicode_map_bytes = bytes(val for x in unicode_map for val in x.to_bytes(2, 'little'))
	outfile.write(unicode_map_bytes)
