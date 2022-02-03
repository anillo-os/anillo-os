#!/usr/bin/env python

import sys

from common import *

x86_64_base = len(syscalls)

syscalls.extend([
	Syscall(x86_64_base + 0, 'thread_set_fs', address='*'),
	Syscall(x86_64_base + 1, 'thread_set_gs', address='*'),
])
