#!/usr/bin/env python3

import sys
from typing import List

args = sys.argv
final_args: List[str] = []

arch = 'unknown'
skip_next = False

for index, arg in enumerate(args):
	if arg == '-arch':
		arch = args[index + 1]
	elif arg == '-flavor':
		skip_next = True
		continue
	elif skip_next:
		continue

	final_args += [arg]
