#!/usr/bin/env python3

from syscall import *

enums.extend([
	Enum('timeout_type', 'u8', [
		('none', '0'),
		('ns_relative', '1'),
		('ns_absolute_monotonic', '2'),
	]),
	Enum('channel_receive_flags', 'u64', prefix='channel_receive_flag', values=[
		('no_wait', '1 << 0'),
		('pre_receive_peek', '1 << 1'),
		('match_message_id', '1 << 2'),
	]),
	Enum('monitor_update_flags', 'u64', prefix='monitor_update_flag', values=[
		('fail_fast', '1 << 0'),
	]),
	Enum('monitor_poll_flags', 'u64', prefix='monitor_poll_flag', values=[
		('reserved', '1 << 0'),
	]),
	Enum('monitor_item_id', 'u64', [
		('none', '0'),
	]),
	Enum('monitor_update_item_flags', 'u64', prefix='monitor_update_item_flag', values=[
		('create', '1 << 0'),
		('delete', '1 << 1'),
		('update', '1 << 2'),

		('enabled', '1 << 3'),
		('disable_on_trigger', '1 << 4'),

		('level_triggered', '0 << 5'),
		('edge_triggered', '1 << 5'),
		('active_high', '0 << 6'),
		('active_low', '1 << 6'),

		('keep_alive', '1 << 7'),

		('defer_delete', '1 << 8'),
		('delete_on_trigger', '1 << 9'),
		('strict_match', '1 << 10'),

		('set_user_flag', '1 << 11'),
	]),
	Enum('monitor_item_type', 'u8', [
		('invalid', '0'),
		('channel', '1'),
		# 2 is reserved
		('futex', '3'),
		('timeout', '4'),
	]),
	Enum('monitor_events', 'u64', prefix='monitor_event', values=[
		('item_deleted', '1 << 0'),

		('channel_message_arrived', '1 << 1'),
		('channel_queue_emptied', '1 << 2'),
		('channel_peer_queue_emptied', '1 << 3'),
		('channel_peer_closed', '1 << 4'),
		('channel_peer_queue_space_available', '1 << 5'),

		('futex_awoken', '1 << 1'),

		('timeout_expired', '1 << 1'),
	]),
	Enum('monitor_event_flags', 'u64', prefix='monitor_event_flag', values=[
		('user', '1 << 0'),
	]),
	Enum('page_allocate_flags', 'u64', prefix='page_allocate_flag', values=[
		('contiguous', '1 << 0'),
		('prebound', '1 << 1'),
		('unswappable', '1 << 2'),
		('uncacheable', '1 << 3'),
	]),
	Enum('page_allocate_shared_flags', 'u64', prefix='page_allocate_shared_flag', values=[
		('xxx_reserved', '0'),
	]),
	Enum('page_map_shared_flags', 'u64', prefix='page_map_shared_flag', values=[
		('xxx_reserved', '0'),
	]),
	Enum('page_permissions', 'u8', prefix='page_permission', values=[
		('read', '1 << 0'),
		('write', '1 << 1'),
		('execute', '1 << 2'),
	]),
	Enum('signal_configuration_flags', 'u64', prefix='signal_configuration_flag', values=[
		('enabled', '1 << 0'),
		('coalesce', '1 << 1'),
		('autorestart', '1 << 2'),
		('allow_redirection', '1 << 3'),
		('preempt', '1 << 4'),
		('block_on_redirect', '1 << 5'),
		('mask_on_handle', '1 << 6'),
		('kill_if_unhandled', '1 << 7'),
	]),
	Enum('signal_info_flags', 'u64', prefix='signal_info_flag', values=[
		('blocked', '1 << 0'),
	]),
	Enum('signal_stack_flags', 'u64', prefix='signal_stack_flag', values=[
		('clear_on_use', '1 << 0'),
	]),
	Enum('process_create_flags', 'u64', prefix='process_create_flag', values=[
		('use_default_stack', '1 << 0'),
	]),
	Enum('channel_message_attachment_data_flags', 'u64', prefix='channel_message_attachment_data_flag', values=[
		('shared', '1 << 0'),
	]),
])

structures.extend([
	Structure('channel_message_attachment_header', [
		('next_offset', 'u64'),
		('length', 'u64'),
		('type', '!fchannel_message_attachment_type_t'),
	]),
	Structure('channel_message_attachment_null', [
		('header', 's:channel_message_attachment_header'),
	]),
	Structure('channel_message_attachment_channel', [
		('header', 's:channel_message_attachment_header'),
		('channel_id', 'u64'),
	]),
	Structure('channel_message_attachment_mapping', [
		('header', 's:channel_message_attachment_header'),
		('mapping_id', 'u64'),
	]),
	Structure('channel_message_attachment_data', [
		('header', 's:channel_message_attachment_header'),
		('flags', 'e:channel_message_attachment_data_flags'),
		('length', 'u64'),
		# descriptor (for shared data) or pointer (for non-shared data)
		('target', 'u64'),
	]),
	Structure('channel_message', [
		('conversation_id', '!fchannel_conversation_id_t'),
		('message_id', '!fchannel_message_id_t'),
		('body_address', 'u64'),
		('body_length', 'u64'),
		('attachments_address', 'u64'),
		('attachments_length', 'u64'),
	]),
	Structure('monitor_item_header', [
		('id', 'e:monitor_item_id'),
		('descriptor_id', 'u64'),
		('type', 'e:monitor_item_type'),
		('context', 'u64'),
	]),
	Structure('monitor_update_item', [
		('header', 's:monitor_item_header'),
		('flags', 'e:monitor_update_item_flags'),
		('events', 'e:monitor_events'),
		('status', '!ferr_t'),
		('data1', 'u64'),
		('data2', 'u64'),
	]),
	Structure('monitor_event', [
		('header', 's:monitor_item_header'),
		('events', 'e:monitor_events'),
		('flags', 'e:monitor_event_flags'),
	]),
	Structure('signal_configuration', [
		('flags', 'e:signal_configuration_flags'),
		('handler', '*'),
		('context', '*'),
	]),
	Structure('signal_mapping', [
		('block_all_flag', '*[u8]'),
		('bus_error_signal', 'u64'),
		('page_fault_signal', 'u64'),
		('floating_point_exception_signal', 'u64'),
		('illegal_instruction_signal', 'u64'),
		('debug_signal', 'u64'),
		('division_by_zero_signal', 'u64'),
	]),
	Structure('signal_info', [
		('flags', 'e:signal_info_flags'),
		('signal_number', 'u64'),
		('thread_id', 'u64'),
		('thread_context', '*[!ferro_thread_context_t]'),
		('data', 'u64'),
		('mask', 'u64'),
	]),
	Structure('signal_stack', [
		('flags', 'e:signal_stack_flags'),
		('base', '*'),
		('size', 'u64'),
	]),
	Structure('memory_region', [
		('start', '*'),
		('length', 'u64'),
	]),
	Structure('process_memory_region', [
		('source', 's:memory_region'),
		('destination', '*'),
	]),
	Structure('process_create_info', [
		('regions', '*c[s:process_memory_region]'),
		('region_count', 'u64'),
		('thread_context', '*c[!ferro_thread_context_t]'),
		('flags', 'e:process_create_flags'),
		('descriptors', '*c[u64]'),
		('descriptor_count', 'u64'),
	]),
])

(syscalls
	.add_syscall('exit', status='i32')
	.add_syscall('log', message='string', message_length='u64')
	.add_syscall('page_allocate', page_count='u64', flags='e:page_allocate_flags', alignment_power='u8', out_address='*')
	.add_syscall('page_free', address='*')
	.add_syscall('page_allocate_shared', page_count='u64', flags='e:page_allocate_shared_flags', out_mapping_id='*[u64]')
	.add_syscall('page_map_shared', mapping_id='u64', page_count='u64', page_offset_count='u64', flags='e:page_map_shared_flags', alignment_power='u8', out_address='*')
	.add_syscall('page_close_shared', mapping_id='u64')
	.add_syscall('page_bind_shared', mapping_id='u64', page_count='u64', page_offset_count='u64', address='*')
	.add_syscall('page_count_shared', mapping_id='u64', out_page_count='*[u64]')
	.add_syscall('page_translate', address='*c', out_phys_address='*[u64]')
	.add_syscall('page_protect', address='*c', page_count='u64', permissions='e:page_permissions')
	.add_syscall('thread_create', stack='*', stack_size='u64', entry='*c', out_thread_id='*[u64]')
	.add_syscall('thread_id', out_thread_id='*[u64]')
	.add_syscall('thread_kill', thread_id='u64')
	.add_syscall('thread_suspend', thread_id='u64', timeout='u64', timeout_type='e:timeout_type')
	.add_syscall('thread_resume', thread_id='u64')
	.add_syscall('thread_block', thread_id='u64')
	.add_syscall('thread_unblock', thread_id='u64')
	.add_syscall('thread_yield', thread_id='u64')
	.add_syscall('thread_execution_context', thread_id='u64', new_context='*c[!ferro_thread_context_t]', out_old_context='*[!ferro_thread_context_t]')
	.add_syscall('thread_signal_configure', thread_id='u64', signal_number='u64', new_configuration='*c[s:signal_configuration]', out_old_configuration='*[s:signal_configuration]')
	.add_syscall('thread_signal_return', info='*c[s:signal_info]')
	.add_syscall('thread_signal', target_thread_id='u64', signal_number='u64')
	.add_syscall('thread_signal_update_mapping', thread_id='u64', new_mapping='*c[s:signal_mapping]', out_old_mapping='*[s:signal_mapping]')
	.add_syscall('thread_signal_stack', new_stack='*c[s:signal_stack]', old_stack='*[s:signal_stack]')
	.add_syscall('futex_wait', address='*[u64]', channel='u64', expected_value='u64', timeout='u64', timeout_type='e:timeout_type', flags='u64')
	.add_syscall('futex_wake', address='*[u64]', channel='u64', wakeup_count='u64', flags='u64')
	.add_syscall('futex_associate', address='*[u64]', channel='u64', event='u64', value='u64')
	.add_syscall('process_create', info='*c[s:process_create_info]', out_process_handle='*[u64]')
	.add_syscall('process_current', out_process_handle='*[u64]')
	.add_syscall('process_id', process_handle='u64', out_process_id='*[u64]')
	.add_syscall('process_kill', process_handle='u64')
	.add_syscall('process_suspend', process_handle='u64')
	.add_syscall('process_resume', process_handle='u64')
	.add_syscall('process_close', process_handle='u64')
	.add_syscall('channel_create_pair', out_channel_ids='*[u64]')
	.add_syscall('channel_conversation_create', channel_id='u64', out_conversation_id='*[!fchannel_conversation_id_t]')
	.add_syscall('channel_send', channel_id='u64', flags='!fchannel_send_flags_t', timeout='u64', timeout_type='e:timeout_type', in_out_message='*[s:channel_message]')
	.add_syscall('channel_receive', channel_id='u64', flags='e:channel_receive_flags', timeout='u64', timeout_type='e:timeout_type', in_out_message='*[s:channel_message]')
	.add_syscall('channel_close', channel_id='u64', release_descriptor='u8')
	.add_syscall('monitor_create', out_monitor_handle='*[u64]')
	.add_syscall('monitor_close', monitor_handle='u64')
	.add_syscall('monitor_update', monitor_handle='u64', flags='e:monitor_update_flags', in_out_items='*[s:monitor_update_item]', in_out_item_count='*[u64]')
	.add_syscall('monitor_poll', monitor_handle='u64', flags='e:monitor_poll_flags', timeout='u64', timeout_type='e:timeout_type', out_events='*[s:monitor_event]', in_out_event_count='*[u64]')
	.add_syscall('constants', out_constants='*[!ferro_constants_t]')
)
