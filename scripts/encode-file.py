#!/usr/bin/env python3

import os
import sys

SCRIPT_DIR = os.path.dirname(__file__)
SOURCE_ROOT = os.path.join(SCRIPT_DIR, '..')

sys.path.append(os.path.join(SOURCE_ROOT, 'scripts'))
import anillo_util

if len(sys.argv) != 4:
	print('Usage: ' + sys.argv[0] + ' <input> <output> <array-name>')
	sys.exit(1)

INPUT_PATH = sys.argv[1]
OUTPUT_PATH = sys.argv[2]
ARRAY_NAME = sys.argv[3]

contents = []
with open(INPUT_PATH, 'rb') as file:
	contents = bytearray(file.read())

anillo_util.mkdir_p(os.path.dirname(OUTPUT_PATH))

with open(OUTPUT_PATH, 'wb') as outfile:
	outfile.write(('#pragma once\n\n').encode())
	outfile.write(anillo_util.to_c_array(ARRAY_NAME, contents).encode())
