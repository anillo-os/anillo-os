/*
 * This file is part of Anillo OS
 * Copyright (C) 2022 Anillo OS Developers
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <gen/ferro/userspace/syscall-handlers.h>
#include <ferro/core/threads.h>
#include <ferro/userspace/threads.private.h>
#include <libsimple/general.h>
#include <ferro/core/scheduler.h>
#include <ferro/userspace/processes.h>
#include <ferro/core/mempool.h>

#define SPECIAL_SIGNAL_CONFIG (fsyscall_signal_configuration_flag_preempt | fsyscall_signal_configuration_flag_kill_if_unhandled | fsyscall_signal_configuration_flag_block_on_redirect)

// must be called with the signal mutex held
static bool is_special_signal(futhread_data_private_t* private_data, uint64_t signal) {
	return (
		signal == private_data->signal_mapping.bus_error_signal                ||
		signal == private_data->signal_mapping.page_fault_signal               ||
		signal == private_data->signal_mapping.floating_point_exception_signal ||
		signal == private_data->signal_mapping.illegal_instruction_signal      ||
		signal == private_data->signal_mapping.debug_signal
	);
};

ferr_t fsyscall_handler_thread_signal_configure(uint64_t thread_id, uint64_t signal_number, const fsyscall_signal_configuration_t* new_configuration, fsyscall_signal_configuration_t* out_old_configuration) {
	ferr_t status = ferr_ok;
	fthread_t* uthread = fsched_find(thread_id, true);

	if (!uthread) {
		status = ferr_no_such_resource;
		goto out_unlocked;
	}

	futhread_data_t* data = futhread_data_for_thread(uthread);
	futhread_data_private_t* private_data = (void*)data;
	bool created = false;
	futhread_signal_handler_t* handler = NULL;

	if (!data || signal_number == 0) {
		status = ferr_invalid_argument;
		goto out_unlocked;
	}

	// TODO: support restartable signals
	if (new_configuration && (new_configuration->flags & fsyscall_signal_configuration_flag_autorestart) != 0) {
		status = ferr_unsupported;
		goto out_unlocked;
	}

	flock_mutex_lock(&private_data->signals_mutex);

	status = simple_ghmap_lookup_h(&private_data->signal_handler_table, signal_number, !!new_configuration, sizeof(*handler), &created, (void*)&handler, NULL);
	if (status != ferr_ok) {
		if (!!new_configuration) {
			goto out;
		} else {
			status = ferr_ok;
			if (out_old_configuration) {
				simple_memset(out_old_configuration, 0, sizeof(*out_old_configuration));
			}
			goto out;
		}
	}

	if (out_old_configuration) {
		if (created) {
			simple_memset(out_old_configuration, 0, sizeof(*out_old_configuration));
		} else {
			simple_memcpy(out_old_configuration, &handler->configuration, sizeof(*out_old_configuration));
		}
	}

	if (new_configuration) {
		if (is_special_signal(private_data, signal_number) && (new_configuration->flags & SPECIAL_SIGNAL_CONFIG) != SPECIAL_SIGNAL_CONFIG) {
			status = ferr_invalid_argument;
			goto out;
		}

		handler->signal = signal_number;
		simple_memcpy(&handler->configuration, new_configuration, sizeof(handler->configuration));
	}

out:
	flock_mutex_unlock(&private_data->signals_mutex);
out_unlocked:
	return status;
};

#if FERRO_ARCH == FERRO_ARCH_x86_64
	#define FTHREAD_EXTRA_SAVE_SIZE FARCH_PER_CPU(xsave_area_size)
#elif FERRO_ARCH == FERRO_ARCH_aarch64
	#define FTHREAD_EXTRA_SAVE_SIZE 0
#endif

ferr_t fsyscall_handler_thread_signal_return(const fsyscall_signal_info_t* info) {
	ferr_t status = ferr_ok;
	fthread_t* uthread = fthread_current();
	futhread_data_t* data = futhread_data_for_thread(uthread);
	futhread_data_private_t* private_data = (void*)data;
	fthread_t* target_uthread = fsched_find(info->thread_id, true);

	if (!target_uthread) {
		status = ferr_no_such_resource;
		goto out;
	}

	flock_mutex_lock(&private_data->signals_mutex);
	private_data->signal_mask = info->mask;
	flock_mutex_unlock(&private_data->signals_mutex);

#if FERRO_ARCH == FERRO_ARCH_x86_64
	data->saved_syscall_context->cs = (farch_int_gdt_index_code_user * 8) | 3;
	data->saved_syscall_context->ss = (farch_int_gdt_index_data_user * 8) | 3;

	data->saved_syscall_context->rax = info->thread_context->rax;
	data->saved_syscall_context->rcx = info->thread_context->rcx;
	data->saved_syscall_context->rdx = info->thread_context->rdx;
	data->saved_syscall_context->rbx = info->thread_context->rbx;
	data->saved_syscall_context->rsi = info->thread_context->rsi;
	data->saved_syscall_context->rdi = info->thread_context->rdi;
	data->saved_syscall_context->rsp = info->thread_context->rsp;
	data->saved_syscall_context->rbp = info->thread_context->rbp;
	data->saved_syscall_context->r8 = info->thread_context->r8;
	data->saved_syscall_context->r9 = info->thread_context->r9;
	data->saved_syscall_context->r10 = info->thread_context->r10;
	data->saved_syscall_context->r11 = info->thread_context->r11;
	data->saved_syscall_context->r12 = info->thread_context->r12;
	data->saved_syscall_context->r13 = info->thread_context->r13;
	data->saved_syscall_context->r14 = info->thread_context->r14;
	data->saved_syscall_context->r15 = info->thread_context->r15;
	data->saved_syscall_context->rip = info->thread_context->rip;

	// only allow userspace to modify the following CPU flags:
	//   * carry (bit 0)
	//   * parity (bit 2)
	//   * adjust (bit 4)
	//   * zero (bit 6)
	//   * sign (bit 7)
	//   * direction (bit 10)
	//   * overflow (bit 11)
	//
	// additionally, we always OR in the following flags:
	//   * always-one (bit 1)
	//   * interrupt-enable (bit 9)
	data->saved_syscall_context->rflags = (info->thread_context->rflags & 0xcd5) | 0x202;

	// now copy the xsave area
	// TODO: verify that the xsave area is valid; this just means we need to validate the xsave header
	simple_memcpy(data->saved_syscall_context->xsave_area, info->thread_context->xsave_area, FARCH_PER_CPU(xsave_area_size));
#elif FERRO_ARCH == FERRO_ARCH_aarch64
	data->saved_syscall_context->x0 = info->thread_context->x0;
	data->saved_syscall_context->x1 = info->thread_context->x1;
	data->saved_syscall_context->x2 = info->thread_context->x2;
	data->saved_syscall_context->x3 = info->thread_context->x3;
	data->saved_syscall_context->x4 = info->thread_context->x4;
	data->saved_syscall_context->x5 = info->thread_context->x5;
	data->saved_syscall_context->x6 = info->thread_context->x6;
	data->saved_syscall_context->x7 = info->thread_context->x7;
	data->saved_syscall_context->x8 = info->thread_context->x8;
	data->saved_syscall_context->x9 = info->thread_context->x9;
	data->saved_syscall_context->x10 = info->thread_context->x10;
	data->saved_syscall_context->x11 = info->thread_context->x11;
	data->saved_syscall_context->x12 = info->thread_context->x12;
	data->saved_syscall_context->x13 = info->thread_context->x13;
	data->saved_syscall_context->x14 = info->thread_context->x14;
	data->saved_syscall_context->x15 = info->thread_context->x15;
	data->saved_syscall_context->x16 = info->thread_context->x16;
	data->saved_syscall_context->x17 = info->thread_context->x17;
	data->saved_syscall_context->x18 = info->thread_context->x18;
	data->saved_syscall_context->x19 = info->thread_context->x19;
	data->saved_syscall_context->x20 = info->thread_context->x20;
	data->saved_syscall_context->x21 = info->thread_context->x21;
	data->saved_syscall_context->x22 = info->thread_context->x22;
	data->saved_syscall_context->x23 = info->thread_context->x23;
	data->saved_syscall_context->x24 = info->thread_context->x24;
	data->saved_syscall_context->x25 = info->thread_context->x25;
	data->saved_syscall_context->x26 = info->thread_context->x26;
	data->saved_syscall_context->x27 = info->thread_context->x27;
	data->saved_syscall_context->x28 = info->thread_context->x28;
	data->saved_syscall_context->x29 = info->thread_context->x29;
	data->saved_syscall_context->x30 = info->thread_context->x30;
	data->saved_syscall_context->pc = info->thread_context->pc;
	data->saved_syscall_context->sp = info->thread_context->sp;

	data->saved_syscall_context->fpsr = info->thread_context->fpsr;
	data->saved_syscall_context->fpcr = info->thread_context->fpcr;

	// only allow userspace to modify the following CPU flags:
	//   * negative (bit 31)
	//   * zero (bit 30)
	//   * carry (bit 29)
	//   * overflow (bit 28)
	data->saved_syscall_context->pstate = (info->thread_context->pstate & 0xf0000000ull) | farch_thread_pstate_aarch64 | farch_thread_pstate_el0 | farch_thread_pstate_sp0;

	// now copy the FP registers
	simple_memcpy(data->saved_syscall_context->fp_registers, info->thread_context->fp_registers, sizeof(data->saved_syscall_context->fp_registers));
#endif

	// we need to use a fake interrupt return to restore the entire context without clobbering any registers like we do in a normal syscall return
	private_data->use_fake_interrupt_return = true;

	if (info->flags & fsyscall_signal_info_flag_blocked) {
		// we're responsible for unblocking the target uthread
		FERRO_WUR_IGNORE(fthread_unblock(target_uthread));
	}

out:
	if (target_uthread) {
		fthread_release(target_uthread);
	}
	return status;
};

FERRO_STRUCT(thread_signal_iterator_context) {
	fthread_t* target_uthread;
	uint64_t signal_number;
	bool should_kill;
};

static bool thread_signal_iterator(void* _context, fproc_t* process, fthread_t* uthread) {
	thread_signal_iterator_context_t* context = _context;
	ferr_t status = ferr_ok;

	if (uthread == context->target_uthread) {
		// skip this uthread
		return true;
	}

	status = futhread_signal(uthread, context->signal_number, context->target_uthread, futhread_signal_flag_blockable);
	if (status == ferr_ok) {
		return false;
	}

	if (status == ferr_aborted) {
		context->should_kill = true;
	}

	return true;
};

ferr_t fsyscall_handler_thread_signal(uint64_t target_thread_id, uint64_t signal_number) {
	ferr_t status = ferr_ok;
	fthread_t* uthread = fsched_find(target_thread_id, true);

	if (!uthread) {
		status = ferr_no_such_resource;
		goto out;
	}

	status = futhread_signal(uthread, signal_number, uthread, futhread_signal_flag_blockable);

	if (status == ferr_no_such_resource || status == ferr_aborted) {
		// try one of the other threads in its process (if it has one)
		fproc_t* process = futhread_process(uthread);

		if (process) {
			thread_signal_iterator_context_t context = {
				.target_uthread = uthread,
				.signal_number = signal_number,
				.should_kill = (status == ferr_aborted),
			};

			if (fproc_for_each_thread(process, thread_signal_iterator, &context) != ferr_cancelled) {
				status = (context.should_kill) ? ferr_aborted : ferr_no_such_resource;
			} else {
				status = ferr_ok;
			}
		}
	}

	if (status == ferr_aborted) {
		// kill the target thread/process
		fproc_t* process = futhread_process(uthread);

		if (process) {
			fproc_kill(process);
		} else {
			FERRO_WUR_IGNORE(fthread_kill(uthread));
		}

		status = ferr_ok;
	}

out:
	return status;
};

// must be called with the signal mutex held
static bool check_valid_handler_for_special_signal(futhread_data_private_t* private_data, uint64_t signal_number) {
	futhread_signal_handler_t* handler = NULL;

	if (signal_number != 0 && simple_ghmap_lookup_h(&private_data->signal_handler_table, signal_number, false, 0, NULL, (void*)&handler, NULL) == ferr_ok) {
		return (handler->configuration.flags & SPECIAL_SIGNAL_CONFIG) == SPECIAL_SIGNAL_CONFIG;
	}

	return true;
};

ferr_t fsyscall_handler_thread_signal_update_mapping(uint64_t thread_id, fsyscall_signal_mapping_t const* new_mapping, fsyscall_signal_mapping_t* out_old_mapping) {
	ferr_t status = ferr_ok;
	fthread_t* uthread = fsched_find(thread_id, true);

	if (!uthread) {
		status = ferr_no_such_resource;
		goto out_unlocked;
	}

	futhread_data_t* data = futhread_data_for_thread(uthread);
	futhread_data_private_t* private_data = (void*)data;

	flock_mutex_lock(&private_data->signals_mutex);

	// FIXME: we should not access userspace memory directly
	//        (this includes reading from the flag later on)

	if (out_old_mapping) {
		simple_memcpy(out_old_mapping, &private_data->signal_mapping, sizeof(*out_old_mapping));
	}

	if (new_mapping) {
		// check that the signals have valid configurations
		if (!(
			check_valid_handler_for_special_signal(private_data, new_mapping->bus_error_signal)                &&
			check_valid_handler_for_special_signal(private_data, new_mapping->page_fault_signal)               &&
			check_valid_handler_for_special_signal(private_data, new_mapping->floating_point_exception_signal) &&
			check_valid_handler_for_special_signal(private_data, new_mapping->illegal_instruction_signal)      &&
			check_valid_handler_for_special_signal(private_data, new_mapping->debug_signal)
		)) {
			status = ferr_invalid_argument;
			goto out;
		}

		simple_memcpy(&private_data->signal_mapping, new_mapping, sizeof(private_data->signal_mapping));
	}

out:
	flock_mutex_unlock(&private_data->signals_mutex);
out_unlocked:
	return status;
};

ferr_t fsyscall_handler_thread_signal_stack(const fsyscall_signal_stack_t* new_stack, fsyscall_signal_stack_t* old_stack) {
	ferr_t status = ferr_ok;
	fthread_t* uthread = fthread_current();
	futhread_data_t* data = futhread_data_for_thread(uthread);
	futhread_data_private_t* private_data = (void*)data;

	flock_mutex_lock(&private_data->signals_mutex);

	if (old_stack) {
		simple_memcpy(old_stack, &private_data->signal_stack, sizeof(*old_stack));
	}

	if (new_stack) {
		simple_memcpy(&private_data->signal_stack, new_stack, sizeof(private_data->signal_stack));
	}

out:
	flock_mutex_unlock(&private_data->signals_mutex);
out_unlocked:
	return status;
};
