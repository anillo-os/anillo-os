#!/usr/bin/env python3

from syscall import *

common_base = len(syscalls)

syscalls.extend([
	Syscall(common_base + 0, 'exit', status='i32'),
	Syscall(common_base + 1, 'log', message='string', message_length='u64'),
	Syscall(common_base + 2, 'page_allocate_any', page_count='u64', flags='u64', out_address='*'),
	Syscall(common_base + 3, 'page_free', address='*'),
	Syscall(common_base + 4, 'fd_open_special', special_id='u64', out_fd='*[u64]'),
	Syscall(common_base + 5, 'fd_close', fd='u64'),
	Syscall(common_base + 6, 'fd_read', fd='u64', offset='u64', desired_length='u64', out_buffer='*', out_read_length='*[u64]'),
	Syscall(common_base + 7, 'fd_write', fd='u64', offset='u64', desired_length='u64', buffer='*c', out_written_length='*[u64]'),
	Syscall(common_base + 8, 'fd_copy_path', fd='u64', buffer_size='u64', out_buffer='mut_string', out_actual_size='*[u64]'),
	Syscall(common_base + 9, 'fd_list_children_init', fd='u64', out_context='*[u64]'),
	Syscall(common_base + 10, 'fd_list_children_finish', context='u64'),
	Syscall(common_base + 11, 'fd_list_children', context='u64', string_size='u64', out_string='*', out_read_count='*[u64]'),
	Syscall(common_base + 12, 'fd_open', path='string', path_length='u64', flags='u64', out_fd='*[u64]'),
	Syscall(common_base + 13, 'thread_create', stack='*', stack_size='u64', entry='*c', out_thread_id='*[u64]'),
	Syscall(common_base + 14, 'thread_id', out_thread_id='*[u64]'),
	Syscall(common_base + 15, 'thread_kill', thread_id='u64'),
	Syscall(common_base + 16, 'thread_suspend', thread_id='u64', timeout='u64', timeout_type='u8'),
	Syscall(common_base + 17, 'thread_resume', thread_id='u64'),
	Syscall(common_base + 18, 'futex_wait', address='*[u64]', channel='u64', expected_value='u64', timeout='u64', timeout_type='u8', flags='u64'),
	Syscall(common_base + 19, 'futex_wake', address='*[u64]', channel='u64', wakeup_count='u64', flags='u64'),
	Syscall(common_base + 20, 'futex_associate', address='*[u64]', channel='u64', event='u64', value='u64'),
	Syscall(common_base + 21, 'process_create', fd='u64', context_block='*c', context_block_size='u64', out_process_id='*[u64]'),
	Syscall(common_base + 22, 'process_id', out_process_id='*[u64]'),
	Syscall(common_base + 23, 'process_kill', process_id='u64'),
	Syscall(common_base + 24, 'process_suspend', process_id='u64'),
	Syscall(common_base + 25, 'process_resume', process_id='u64'),
])
