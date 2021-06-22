#!/usr/bin/env python

import os
import struct
import errno

ARCH=os.environ.get('ARCH', 'x86_64')

SCRIPT_DIR = os.path.dirname(__file__)
SOURCE_ROOT = os.path.join(SCRIPT_DIR, '..', '..')
KERNEL_SOURCE_ROOT = os.path.join(SOURCE_ROOT, 'kernel')
BUILD_DIR = os.path.join(SOURCE_ROOT, 'build', ARCH, 'kernel')
FONT_PATH = os.path.join(KERNEL_SOURCE_ROOT, 'resources', 'Lat15-TerminusBold32x16.psf')
OUTPUT_PATH = os.path.join(BUILD_DIR, 'include', 'gen', 'ferro', 'font.h')
HEADER_GUARD_NAME = '_GEN_FERRO_FONT_H'

PSF2_MAGIC = bytearray([0x72, 0xb5, 0x4a, 0x86])
PSF_UNICODE_FLAG = 1

# from https://stackoverflow.com/a/600612/6620880
def mkdir_p(path):
	try:
		os.makedirs(path)
	except OSError as exc:
		if not (exc.errno == errno.EEXIST and os.path.isdir(path)):
			raise

# adapted from https://stackoverflow.com/a/53808695/6620880
def to_padded_hex(value, hex_digits=2):
	return '0x{0:0{1}x}'.format(value if isinstance(value, int) else ord(value), hex_digits)

def to_c_array(array_name, values, array_type='uint8_t', formatter=to_padded_hex, column_count=8, static=True):
	values = [formatter(v) for v in values]
	rows = [values[i:i + column_count] for i in range(0, len(values), column_count)]
	body = ',\n\t'.join([', '.join(r) for r in rows])
	return '{}{} {}[] = {{\n\t{},\n}};\n'.format('static ' if static else '', array_type, array_name, body)

contents = []
with open(FONT_PATH, 'rb') as file:
	contents = bytearray(file.read())

header = struct.unpack('4BIIIIIII', contents[0:32])
magic = bytearray(header[0:4])
version = header[4]
header_size = header[5]
flags = header[6]
glyph_count = header[7]
glyph_size = header[8]
glyph_height = header[9]
glyph_width = header[10]

if magic != PSF2_MAGIC:
	print('Invalid PSF2 magic')
	exit(1)

table_offset = header_size + (glyph_size * glyph_count)
unicode_map = [0] * 0xffff

if flags & PSF_UNICODE_FLAG:
	table = contents[table_offset:]

	# general process derived from https://wiki.osdev.org/PC_Screen_Font

	table_index = 0
	glyph_index = 0
	while table_index < len(table):
		curr = table[table_index]
		if curr == 0xff:
			table_index += 1
			glyph_index += 1
			continue
		elif curr & 0x80:
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
		unicode_map[curr] = glyph_index
		table_index += 1
else:
	unicode_map = []

mkdir_p(os.path.dirname(OUTPUT_PATH))

with open(OUTPUT_PATH, 'wb') as outfile:
	outfile.write('#ifndef ' + HEADER_GUARD_NAME + '\n#define ' + HEADER_GUARD_NAME + '\n\n#include <stdint.h>\n\n')
	outfile.write(to_c_array('font_data', contents[0:table_offset]))
	outfile.write(to_c_array('unicode_map', unicode_map, 'uint16_t', lambda v: to_padded_hex(v, 4)))
	outfile.write('\n#endif // ' + HEADER_GUARD_NAME + '\n')
