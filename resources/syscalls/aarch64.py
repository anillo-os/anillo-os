#!/usr/bin/env python

import sys

from common import *

(syscalls
	.add_syscall('thread_set_tpidr', address='*')
)
