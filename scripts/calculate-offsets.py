#!/usr/bin/env python3

from dataclasses import dataclass
import os
import argparse
from sys import stderr
from typing import List
import subprocess
import re
import io
import hashlib
import anillo_util
import json

SCRIPT_DIR = os.path.dirname(__file__)

argparser = argparse.ArgumentParser()
argparser.add_argument('-a', '--arch', required=True, choices=['x86_64', 'aarch64'], help='The architecture to generate offsets for')
argparser.add_argument('-s', '--source', required=True, help='A path for the input source file to use to generate offsets')
argparser.add_argument('-H', '--header', required=True, help='A path for the resulting header file')
argparser.add_argument('-j', '--json', help='A path for the resulting JSON file. If omitted, no JSON file will be created')
argparser.add_argument('-d', '--depfile', help='A path for the resulting dependency list file. If omitted, no dependency list file will be created')
argparser.add_argument('compiler_args', nargs='*', help='A list of arguments to pass to the compiler')
args = argparser.parse_args()

arch: str = args.arch
source: str = os.path.abspath(args.source)
header: str = os.path.abspath(args.header)
jsonpath: str | None = args.json
if jsonpath != None:
	jsonpath = os.path.abspath(jsonpath)
depfile: str | None = args.depfile
if depfile != None:
	depfile = os.path.abspath(depfile)
compiler_args: List[str] = args.compiler_args

compiler_args.append(f'-I{os.path.join(SCRIPT_DIR, "include")}')

result = subprocess.run([
	'clang',
	'-o-',
	'-ffreestanding',
	'-nostdlib',
	'-target',
	f'{arch}-unknown-darwin-macho',
	'-S',
	source,
	*compiler_args
], stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=stderr)
result.check_returncode()

output = result.stdout.decode('utf-8')

@dataclass
class Offset:
	struct: str
	member: str
	offset: int

@dataclass
class Size:
	struct: str
	size: int

SIZE_REGEX = r'(?:##|;) XXX ([A-Za-z_][A-Za-z_0-9]*) = ([0-9]+)'
OFFSET_REGEX = r'(?:##|;) XXX ([A-Za-z_][A-Za-z_0-9]*) XXX ([A-Za-z_][A-Za-z_0-9]*) = ([0-9]+)'

sizes = [Size(x.group(1), int(x.group(2))) for x in re.finditer(SIZE_REGEX, output)]
offsets = [Offset(x.group(1), x.group(2), int(x.group(3))) for x in re.finditer(OFFSET_REGEX, output)]

header_contents = '#pragma once\n\n'

for size in sizes:
	header_contents += f'#define FLAYOUT_{size.struct}_SIZE {size.size}\n'

for offset in offsets:
	header_contents += f'#define FOFFSET_{offset.struct}_{offset.member} {offset.offset}\n'

write_header = False

anillo_util.mkdir_p(os.path.dirname(header))

if os.path.exists(header):
	with io.open(header, 'r', newline='\n') as outfile:
		file_content = outfile.read().strip()
		if hashlib.sha256(header_contents.strip().encode()).hexdigest() != hashlib.sha256(file_content.encode()).hexdigest():
			write_header = True
else:
	write_header = True

if write_header:
	with io.open(header, 'w', newline='\n') as outfile:
		outfile.write(header_contents)

if jsonpath != None:
	data = {}

	for size in sizes:
		data[size.struct] = { 'size': size.size, 'layout': {} }

	for offset in offsets:
		if not (offset.struct in data):
			data[offset.struct] = { 'size': None, 'layout': {} }

		data[offset.struct]['layout'][offset.member] = offset.offset

	anillo_util.mkdir_p(os.path.dirname(jsonpath))

	with io.open(jsonpath, 'w', newline='\n') as jsonfile:
		json.dump(data, jsonfile, indent='\t')

if depfile != None:
	result = subprocess.run([
		'clang',
		'-o-',
		'-ffreestanding',
		'-nostdlib',
		'-target',
		f'{arch}-unknown-darwin-macho',
		'-M',
		source,
		*compiler_args
	], stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=stderr)
	result.check_returncode()

	output = result.stdout.decode('utf-8')

	write_depfile = False

	anillo_util.mkdir_p(os.path.dirname(depfile))

	if os.path.exists(depfile):
		with io.open(depfile, 'r', newline='\n') as outfile:
			file_content = outfile.read().strip()
			if hashlib.sha256(output.strip().encode()).hexdigest() != hashlib.sha256(file_content.encode()).hexdigest():
				write_depfile = True
	else:
		write_depfile = True

	if write_depfile:
		with io.open(depfile, 'w', newline='\n') as outfile:
			outfile.write(output)
