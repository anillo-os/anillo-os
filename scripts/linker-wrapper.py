#!/usr/bin/env python3

import sys
from typing import List
import os

args = sys.argv[1:]
final_args: List[str] = []

arch = 'unknown'
skip_next = False

for index, arg in enumerate(args):
	if arg == '-arch':
		arch = args[index + 1]
	elif arg == '-flavor':
		skip_next = True
		continue
	elif arg == '--eh-frame-hdr':
		continue
	elif arg == '-dead_strip':
		continue
	elif skip_next:
		skip_next = False
		continue

	final_args += [arg]

if arch == 'arm64':
	arch = 'aarch64'

os.execlp(f'{arch}-apple-darwin11-ld', f'{arch}-apple-darwin11-ld', *final_args)
