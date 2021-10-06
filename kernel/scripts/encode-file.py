#!/usr/bin/env python3

import os
import errno

ARCH=os.environ.get('ARCH', 'x86_64')

SCRIPT_DIR = os.path.dirname(__file__)
SOURCE_ROOT = os.path.join(SCRIPT_DIR, '..', '..')
KERNEL_SOURCE_ROOT = os.path.join(SOURCE_ROOT, 'kernel')
BUILD_DIR = os.path.join(SOURCE_ROOT, 'build', ARCH, 'kernel')
INPUT_PATH = os.path.join(KERNEL_SOURCE_ROOT, 'src', 'gdbstub', ARCH, 'target.xml')
OUTPUT_PATH = os.path.join(BUILD_DIR, 'include', 'gen', 'ferro', 'gdbstub', 'target.xml.h')
HEADER_GUARD_NAME = '_GEN_FERRO_GDBSTUB_TARGET_XML_H'

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
	if len(values) == 0:
		return '{}{} {}[] = {{}};\n'.format('static ' if static else '', array_type, array_name)
	values = [formatter(v) for v in values]
	rows = [values[i:i + column_count] for i in range(0, len(values), column_count)]
	body = ',\n\t'.join([', '.join(r) for r in rows])
	return '{}{} {}[] = {{\n\t{},\n}};\n'.format('static ' if static else '', array_type, array_name, body)

contents = []
with open(INPUT_PATH, 'rb') as file:
	contents = bytearray(file.read())

mkdir_p(os.path.dirname(OUTPUT_PATH))

with open(OUTPUT_PATH, 'wb') as outfile:
	outfile.write(('#ifndef ' + HEADER_GUARD_NAME + '\n#define ' + HEADER_GUARD_NAME + '\n\n#include <stdint.h>\n\n').encode())
	outfile.write(to_c_array('target_xml_data', contents).encode())
	outfile.write(('\n#endif // ' + HEADER_GUARD_NAME + '\n').encode())
