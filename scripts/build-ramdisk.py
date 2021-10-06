#!/usr/bin/env python3

import os
import json
import struct
import errno
import sys
import ctypes

ARCH=os.environ.get('ARCH', 'x86_64')
SCRIPT_DIR = os.path.dirname(__file__)
SOURCE_ROOT = os.path.join(SCRIPT_DIR, '..')
KERNEL_SOURCE_ROOT = os.path.join(SOURCE_ROOT, 'kernel')
BUILD_DIR = os.path.join(SOURCE_ROOT, 'build', ARCH)
KERNEL_BUILD_DIR = os.path.join(BUILD_DIR, 'kernel')
OFFSETS_JSON_PATH = os.path.join(KERNEL_BUILD_DIR, 'offsets.json')
OUTPUT_RAMDISK_PATH = os.path.join(BUILD_DIR, 'ramdisk')
INPUT_DIR_PATH = os.path.join(BUILD_DIR, 'ramdisksrc')

DIRECTORY_ENTRY_STRUCT = struct.Struct('= Q Q Q Q L 4x')
SECTION_HEADER_STRUCT = struct.Struct('= H 6x Q Q')

# from https://stackoverflow.com/a/600612/6620880
def mkdir_p(path):
	try:
		os.makedirs(path)
	except OSError as exc:
		if not (exc.errno == errno.EEXIST and os.path.isdir(path)):
			raise

def pack_into_stream(structure, stream, *args):
	stream.write(structure.pack(*args))

def pack_directory_entry(parent_index, name_offset, contents_offset, size, flags):
	return DIRECTORY_ENTRY_STRUCT.pack(parent_index, name_offset, contents_offset, size, flags)

def pack_section_header(type, offset, length):
	return SECTION_HEADER_STRUCT.pack(type, offset, length)

def round_up_whole(number, multiple):
	return (number + (multiple - 1)) & -multiple

offsets = {}

mkdir_p(os.path.dirname(OUTPUT_RAMDISK_PATH))
mkdir_p(INPUT_DIR_PATH)

with open(OFFSETS_JSON_PATH, 'r') as offsets_file:
	offsets = json.load(offsets_file)

RAMDISK_SIZE_OFFSET = int(offsets['ferro_ramdisk']['layout']['ramdisk_size'])
SECTION_COUNT_OFFSET = int(offsets['ferro_ramdisk']['layout']['section_count'])
SECTION_HEADERS_START = int(offsets['ferro_ramdisk']['layout']['section_headers'])
SIZEOF_SECTION_HEADER = int(offsets['ferro_ramdisk_section_header']['size'])
SIZEOF_DIRECTORY_ENTRY = int(offsets['ferro_ramdisk_directory_entry']['size'])
SIZEOF_RAMDISK_HEADER = int(offsets['ferro_ramdisk_header']['size'])
SECTIONS_START = SECTION_HEADERS_START + 3 * SIZEOF_SECTION_HEADER

SECTION_HEADER_TYPE_STRING_TABLE = 0
SECTION_HEADER_TYPE_DIRECTORIES = 1
SECTION_HEADER_TYPE_DATA = 2

string_table = {}
string_table_offset = 0
dir_section = b''

ramdisk = open(OUTPUT_RAMDISK_PATH, 'wb')

ramdisk.seek(SECTION_COUNT_OFFSET)
ramdisk.write((3).to_bytes(8, sys.byteorder))

dir_entry_count = 1

dir_indicies = {
	INPUT_DIR_PATH: 0,
}

dir_content_indicies = {}

# first, collect names and indicies
for root, dirs, files in os.walk(INPUT_DIR_PATH):
	dir_content_indicies[root] = dir_entry_count
	dir_entry_count += len(dirs) + len(files)

	for entry in dirs + files:
		string_table[entry] = string_table_offset
		string_table_offset += len(entry) + 1

ramdisk.seek(SECTIONS_START)

string_table_offset = ramdisk.tell() - SECTIONS_START

# write the string table
for string in string_table:
	ramdisk.write(string.encode() + b'\x00')

# align the section to a multiple of 8
ramdisk.seek(round_up_whole(ramdisk.tell(), 8))

data_section_offset = ramdisk.tell() - SECTIONS_START
data_section_absolute_offset = ramdisk.tell()

dir_section = bytearray(dir_entry_count * SIZEOF_DIRECTORY_ENTRY)

dir_section[0:SIZEOF_DIRECTORY_ENTRY] = pack_directory_entry(ctypes.c_uint64(-1).value, ctypes.c_uint64(-1).value, 1, len(os.listdir(INPUT_DIR_PATH)), 1)

# now build the directory section
for root, dirs, files in os.walk(INPUT_DIR_PATH):
	index = dir_content_indicies[root]

	for dir in dirs:
		# it'd be great if we could avoid the `listdir` here
		dir_indicies[os.path.join(root, dir)] = index
		dir_section[(index * SIZEOF_DIRECTORY_ENTRY):((index + 1) * SIZEOF_DIRECTORY_ENTRY)] = pack_directory_entry(dir_indicies[root], string_table[dir], dir_content_indicies[os.path.join(root, dir)], len(os.listdir(os.path.join(root, dir))), 1)
		index += 1

	for file in files:
		file_size = os.path.getsize(os.path.join(root, file))
		dir_section[(index * SIZEOF_DIRECTORY_ENTRY):((index + 1) * SIZEOF_DIRECTORY_ENTRY)] = pack_directory_entry(dir_indicies[root], string_table[file], ramdisk.tell() - data_section_absolute_offset, file_size, 0)
		index += 1

		with open(os.path.join(root, file), 'rb') as infile:
			while True:
				chunk = infile.read(1024 * 1024)
				if not chunk:
					break
				ramdisk.write(chunk)

# align the section to a multiple of 8
ramdisk.seek(round_up_whole(ramdisk.tell(), 8))

dir_section_offset = ramdisk.tell() - SECTIONS_START

ramdisk.write(dir_section)

# align the section to a multiple of 8
ramdisk.seek(round_up_whole(ramdisk.tell(), 8))

# now go back and write the file size
total_size = ramdisk.tell()
ramdisk.seek(RAMDISK_SIZE_OFFSET)
ramdisk.write((total_size - SIZEOF_RAMDISK_HEADER).to_bytes(8, sys.byteorder))

# now write section headers
ramdisk.seek(SECTION_HEADERS_START)
ramdisk.write(pack_section_header(SECTION_HEADER_TYPE_STRING_TABLE, string_table_offset, data_section_offset - string_table_offset))
ramdisk.write(pack_section_header(SECTION_HEADER_TYPE_DIRECTORIES, dir_section_offset, (total_size - SECTIONS_START) - dir_section_offset))
ramdisk.write(pack_section_header(SECTION_HEADER_TYPE_DATA, data_section_offset, dir_section_offset - data_section_offset))

ramdisk.close()
