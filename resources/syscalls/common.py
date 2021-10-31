#!/usr/bin/env python3

from syscall import *

syscalls.extend([
	Syscall(1, 'exit', [SyscallParameter('i32', 'status')]),
	Syscall(2, 'log', [SyscallParameter('*c', 'message'), SyscallParameter('u64', 'message_length')]),
	Syscall(3, 'page_allocate_any', [SyscallParameter('u64', 'page_count'), SyscallParameter('u64', 'flags'), SyscallParameter('*', 'out_address')]),
	Syscall(4, 'page_free', [SyscallParameter('*', 'address')]),
	Syscall(5, 'fd_open_special', [SyscallParameter('u64', 'special_id'), SyscallParameter('*', 'out_fd')]),
	Syscall(6, 'fd_close', [SyscallParameter('u64', 'fd')]),
	Syscall(7, 'fd_read', [SyscallParameter('u64', 'fd'), SyscallParameter('u64', 'offset'), SyscallParameter('u64', 'desired_length'), SyscallParameter('*', 'out_buffer'), SyscallParameter('*', 'out_read_length')]),
	Syscall(8, 'fd_write', [SyscallParameter('u64', 'fd'), SyscallParameter('u64', 'offset'), SyscallParameter('u64', 'desired_length'), SyscallParameter('*c', 'buffer'), SyscallParameter('*', 'out_written_length')]),
])
