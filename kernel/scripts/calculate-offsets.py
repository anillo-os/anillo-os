#!/usr/bin/env python3

import os
import errno
import subprocess
import re
import json
import io
import sys
import tempfile
import hashlib

SCRIPT_DIR = os.path.dirname(__file__)
SOURCE_ROOT = os.path.join(SCRIPT_DIR, '..', '..')

sys.path.append(os.path.join(SOURCE_ROOT, 'scripts'))
import anillo_util

if len(sys.argv) != 6:
	print('Usage: ' + sys.argv[0] + ' <architecture> <build-dir> <output-header> <output-json> <output-depfile>')
	sys.exit(1)

ARCH = sys.argv[1]
BUILD_DIR = sys.argv[2]
OUTPUT_HEADER_PATH = sys.argv[3]
OUTPUT_JSON_PATH = sys.argv[4]
OUTPUT_DEPFILE_PATH = sys.argv[5]

KERNEL_SOURCE_ROOT = os.path.join(SOURCE_ROOT, 'kernel')
HEADER_GUARD_NAME = '_GEN_FERRO_OFFSETS_H'

offsets_tmp_path = os.path.join(BUILD_DIR, 'offsets.c')
object_tmp_path = os.path.join(BUILD_DIR, 'offsets.o')

HEADERS_COMMON = [
	'ferro/core/interrupts.h',
	'ferro/core/threads.h',
	'ferro/platform.h',
	'ferro/core/ramdisk.h',
	'ferro/core/threads.private.h',
	'ferro/userspace/threads.h',
]

HEADERS_PER_ARCH = {
	'x86_64': [
		'ferro/core/x86_64/smp-init.h',
	],
	'aarch64': [],
}

STRUCTS_COMMON = [
	'fthread_saved_context',
	'fint_frame',

	'ferro_ramdisk',
	'ferro_ramdisk_header',
	'ferro_ramdisk_section_header',
	'ferro_ramdisk_directory_entry',

	'fthread_private',
	'fthread_hook',

	'futhread_data',
]

STRUCTS_PER_ARCH = {
	'x86_64': [
		'farch_int_frame_core',
		'farch_int_frame_with_code',
		'farch_per_cpu_data',
		'farch_smp_init_data',
	],
	'aarch64': [
		'farch_per_cpu_data',
	],
}

data = {}
defs = ''
tmp_index = 0

anillo_util.mkdir_p(os.path.dirname(offsets_tmp_path))

with io.open(offsets_tmp_path, 'w', newline='\n') as tmpfile:
	for header in HEADERS_COMMON + HEADERS_PER_ARCH[ARCH]:
		tmpfile.write('#include <' + header + '>\n')

	tmpfile.write('\n')

	for struct in STRUCTS_COMMON + STRUCTS_PER_ARCH[ARCH]:
		tmpfile.write('struct ' + struct + ' tmp' + str(tmp_index) + ';\n')
		tmp_index = tmp_index + 1

headers_result = subprocess.run(['clang', '-ffreestanding', '-nostdlib', '-target', ARCH + '-unknown-none-macho', '-I', os.path.join(KERNEL_SOURCE_ROOT, 'include'), '-I', os.path.join(KERNEL_SOURCE_ROOT, 'kernel-include'), offsets_tmp_path, '-M'], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)

headers_result.check_returncode()

dep_headers = headers_result.stdout.decode().strip()

write_depfile = False

if os.path.exists(OUTPUT_DEPFILE_PATH):
	with io.open(OUTPUT_DEPFILE_PATH, 'r', newline='\n') as outfile:
		file_content = outfile.read().strip()
		if hashlib.sha256(dep_headers.encode()).hexdigest() != hashlib.sha256(file_content.encode()).hexdigest():
			write_depfile = True
else:
	write_depfile = True

if write_depfile:
	with io.open(OUTPUT_DEPFILE_PATH, 'w', newline='\n') as outfile:
		outfile.write(dep_headers)

result = subprocess.run(['clang', '-Xclang', '-fdump-record-layouts', '-ffreestanding', '-nostdlib', '-target', ARCH + '-unknown-none-macho', '-I', os.path.join(KERNEL_SOURCE_ROOT, 'include'), '-I', os.path.join(KERNEL_SOURCE_ROOT, 'kernel-include'), '-c', '-o', object_tmp_path, offsets_tmp_path, '-emit-llvm'], stdout=subprocess.PIPE)

result.check_returncode()

entries = [x.group() for x in re.finditer(r'\*\*\* Dumping AST Record Layout\n([^\n]|\n(?!\n))*', result.stdout.decode())]

for entry in entries:
	lines = entry.splitlines()
	info = {}

	# remove the first line
	del lines[0]

	# pop important lines
	name_line = lines.pop(0)
	total_line = lines.pop(-1)

	# find the name
	try:
		struct = re.findall(r'struct ([A-Za-z0-9_]+)', name_line)[0]
	except IndexError:
		continue

	info['size'] = re.findall(r'sizeof=([0-9]+)', total_line)[0]
	info['alignment'] = re.findall(r'align=([0-9]+)', total_line)[0]
	info['layout'] = {}

	# parse the layout
	for line in lines:
		try:
			offset = re.findall(r'^\s*([0-9]+)', line)[0]
		except IndexError:
			continue

		try:
			member = re.findall(r'([A-Za-z0-9_]+)$', line)[0]
		except IndexError:
			continue

		info['layout'][member] = offset

	data[struct] = info

for struct in STRUCTS_COMMON + STRUCTS_PER_ARCH[ARCH]:
	if not struct in data:
		print('Failed to find ' + struct)
		exit(1)

for struct in data:
	if not struct in STRUCTS_COMMON + STRUCTS_PER_ARCH[ARCH]:
		continue

	defs += '\n'
	defs += '#define FLAYOUT_' + struct + '_SIZE ' + data[struct]['size'] + '\n'
	defs += '#define FLAYOUT_' + struct + '_ALIGN ' + data[struct]['alignment'] + '\n'

	for member in data[struct]['layout']:
		defs += '#define FOFFSET_' + struct + '_' + member + ' ' + data[struct]['layout'][member] + '\n'

anillo_util.mkdir_p(os.path.dirname(OUTPUT_HEADER_PATH))

output_header_content = '#ifndef ' + HEADER_GUARD_NAME + '\n#define ' + HEADER_GUARD_NAME + '\n' + defs + '\n#endif // ' + HEADER_GUARD_NAME + '\n'

write_header = False

if os.path.exists(OUTPUT_HEADER_PATH):
	with io.open(OUTPUT_HEADER_PATH, 'r', newline='\n') as outfile:
		file_content = outfile.read().strip()
		if hashlib.sha256(output_header_content.strip().encode()).hexdigest() != hashlib.sha256(file_content.encode()).hexdigest():
			write_header = True
else:
	write_header = True

if write_header:
	with io.open(OUTPUT_HEADER_PATH, 'w', newline='\n') as outfile:
		outfile.write(output_header_content)

with io.open(OUTPUT_JSON_PATH, 'w', newline='\n') as jsonfile:
	json.dump(data, jsonfile, indent='\t')
