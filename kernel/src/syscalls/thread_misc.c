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
#include <ferro/core/scheduler.h>
#include <ferro/userspace/threads.private.h>
#include <libsimple/general.h>

ferr_t fsyscall_handler_thread_block(uint64_t thread_id) {
	ferr_t status = ferr_ok;
	fthread_t* thread = fsched_find(thread_id, true);

	if (!thread) {
		status = ferr_no_such_resource;
		goto out;
	}

	status = fthread_block(thread, true);

out:
	if (thread) {
		fthread_release(thread);
	}
	return status;
};

ferr_t fsyscall_handler_thread_unblock(uint64_t thread_id) {
	ferr_t status = ferr_ok;
	fthread_t* thread = fsched_find(thread_id, true);

	if (!thread) {
		status = ferr_no_such_resource;
		goto out;
	}

	status = fthread_unblock(thread);

out:
	if (thread) {
		fthread_release(thread);
	}
	return status;
};

ferr_t fsyscall_handler_thread_execution_context(uint64_t thread_id, ferro_thread_context_t const* new_context, ferro_thread_context_t* out_old_context) {
	ferr_t status = ferr_ok;
	fthread_t* thread = fsched_find(thread_id, true);
	futhread_data_t* data = futhread_data_for_thread(thread);
	futhread_data_private_t* private_data = (void*)data;
	fthread_saved_context_t* saved_context = NULL;

	if (!thread || !data) {
		status = ferr_no_such_resource;
		goto out;
	}

	flock_spin_intsafe_lock(&thread->lock);
	if ((thread->state & fthread_state_blocked) == 0) {
		status = ferr_invalid_argument;
	}
	flock_spin_intsafe_unlock(&thread->lock);

	if (status != ferr_ok) {
		goto out;
	}

	if (fthread_saved_context_is_kernel_space(thread->saved_context)) {
		// use the user context instead
		saved_context = data->saved_syscall_context;

		if (new_context) {
			// make sure the thread uses a fake interrupt return when it's going to return to userspace
			private_data->use_fake_interrupt_return = true;
		}
	} else {
		saved_context = thread->saved_context;
	}

	if (out_old_context) {
		#if FERRO_ARCH == FERRO_ARCH_x86_64
			out_old_context->rax = saved_context->rax;
			out_old_context->rcx = saved_context->rcx;
			out_old_context->rdx = saved_context->rdx;
			out_old_context->rbx = saved_context->rbx;
			out_old_context->rsi = saved_context->rsi;
			out_old_context->rdi = saved_context->rdi;
			out_old_context->rsp = saved_context->rsp;
			out_old_context->rbp = saved_context->rbp;
			out_old_context->r8 = saved_context->r8;
			out_old_context->r9 = saved_context->r9;
			out_old_context->r10 = saved_context->r10;
			out_old_context->r11 = saved_context->r11;
			out_old_context->r12 = saved_context->r12;
			out_old_context->r13 = saved_context->r13;
			out_old_context->r14 = saved_context->r14;
			out_old_context->r15 = saved_context->r15;
			out_old_context->rip = saved_context->rip;
			out_old_context->rflags = saved_context->rflags;

			out_old_context->xsave_area = (void*)fpage_align_address_up((uintptr_t)out_old_context + sizeof(*out_old_context), fpage_round_up_to_alignment_power(64));

			// now copy the xsave area
			simple_memcpy(out_old_context->xsave_area, saved_context->xsave_area, FARCH_PER_CPU(xsave_area_size));
			out_old_context->xsave_area_size = FARCH_PER_CPU(xsave_area_size);
		#elif FERRO_ARCH == FERRO_ARCH_aarch64
			out_old_context->x0 = saved_context->x0;
			out_old_context->x1 = saved_context->x1;
			out_old_context->x2 = saved_context->x2;
			out_old_context->x3 = saved_context->x3;
			out_old_context->x4 = saved_context->x4;
			out_old_context->x5 = saved_context->x5;
			out_old_context->x6 = saved_context->x6;
			out_old_context->x7 = saved_context->x7;
			out_old_context->x8 = saved_context->x8;
			out_old_context->x9 = saved_context->x9;
			out_old_context->x10 = saved_context->x10;
			out_old_context->x11 = saved_context->x11;
			out_old_context->x12 = saved_context->x12;
			out_old_context->x13 = saved_context->x13;
			out_old_context->x14 = saved_context->x14;
			out_old_context->x15 = saved_context->x15;
			out_old_context->x16 = saved_context->x16;
			out_old_context->x17 = saved_context->x17;
			out_old_context->x18 = saved_context->x18;
			out_old_context->x19 = saved_context->x19;
			out_old_context->x20 = saved_context->x20;
			out_old_context->x21 = saved_context->x21;
			out_old_context->x22 = saved_context->x22;
			out_old_context->x23 = saved_context->x23;
			out_old_context->x24 = saved_context->x24;
			out_old_context->x25 = saved_context->x25;
			out_old_context->x26 = saved_context->x26;
			out_old_context->x27 = saved_context->x27;
			out_old_context->x28 = saved_context->x28;
			out_old_context->x29 = saved_context->x29;
			out_old_context->x30 = saved_context->x30;
			out_old_context->pc = saved_context->pc;
			out_old_context->sp = saved_context->sp;
			out_old_context->fpsr = saved_context->fpsr;
			out_old_context->fpcr = saved_context->fpcr;
			out_old_context->pstate = saved_context->pstate;

			out_old_context->fp_registers = (void*)fpage_align_address_up((uintptr_t)out_old_context + sizeof(*out_old_context), fpage_round_up_to_alignment_power(16));

			// now copy the FP registers
			simple_memcpy(out_old_context->fp_registers, saved_context->fp_registers, sizeof(saved_context->fp_registers));
		#endif
	}

	if (new_context) {
		#if FERRO_ARCH == FERRO_ARCH_x86_64
			saved_context->cs = (farch_int_gdt_index_code_user * 8) | 3;
			saved_context->ss = (farch_int_gdt_index_data_user * 8) | 3;

			saved_context->rax = new_context->rax;
			saved_context->rcx = new_context->rcx;
			saved_context->rdx = new_context->rdx;
			saved_context->rbx = new_context->rbx;
			saved_context->rsi = new_context->rsi;
			saved_context->rdi = new_context->rdi;
			saved_context->rsp = new_context->rsp;
			saved_context->rbp = new_context->rbp;
			saved_context->r8 = new_context->r8;
			saved_context->r9 = new_context->r9;
			saved_context->r10 = new_context->r10;
			saved_context->r11 = new_context->r11;
			saved_context->r12 = new_context->r12;
			saved_context->r13 = new_context->r13;
			saved_context->r14 = new_context->r14;
			saved_context->r15 = new_context->r15;
			saved_context->rip = new_context->rip;

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
			saved_context->rflags = (new_context->rflags & 0xcd5) | 0x202;

			// now copy the xsave area
			// TODO: verify that the xsave area is valid; this just means we need to validate the xsave header
			simple_memcpy(saved_context->xsave_area, new_context->xsave_area, FARCH_PER_CPU(xsave_area_size));
		#elif FERRO_ARCH == FERRO_ARCH_aarch64
			saved_context->x0 = new_context->x0;
			saved_context->x1 = new_context->x1;
			saved_context->x2 = new_context->x2;
			saved_context->x3 = new_context->x3;
			saved_context->x4 = new_context->x4;
			saved_context->x5 = new_context->x5;
			saved_context->x6 = new_context->x6;
			saved_context->x7 = new_context->x7;
			saved_context->x8 = new_context->x8;
			saved_context->x9 = new_context->x9;
			saved_context->x10 = new_context->x10;
			saved_context->x11 = new_context->x11;
			saved_context->x12 = new_context->x12;
			saved_context->x13 = new_context->x13;
			saved_context->x14 = new_context->x14;
			saved_context->x15 = new_context->x15;
			saved_context->x16 = new_context->x16;
			saved_context->x17 = new_context->x17;
			saved_context->x18 = new_context->x18;
			saved_context->x19 = new_context->x19;
			saved_context->x20 = new_context->x20;
			saved_context->x21 = new_context->x21;
			saved_context->x22 = new_context->x22;
			saved_context->x23 = new_context->x23;
			saved_context->x24 = new_context->x24;
			saved_context->x25 = new_context->x25;
			saved_context->x26 = new_context->x26;
			saved_context->x27 = new_context->x27;
			saved_context->x28 = new_context->x28;
			saved_context->x29 = new_context->x29;
			saved_context->x30 = new_context->x30;
			saved_context->pc = new_context->pc;
			saved_context->sp = new_context->sp;

			saved_context->fpsr = new_context->fpsr;
			saved_context->fpcr = new_context->fpcr;

			// only allow userspace to modify the following CPU flags:
			//   * negative (bit 31)
			//   * zero (bit 30)
			//   * carry (bit 29)
			//   * overflow (bit 28)
			saved_context->pstate = (new_context->pstate & 0xf0000000ull) | farch_thread_pstate_aarch64 | farch_thread_pstate_el0 | farch_thread_pstate_sp0;

			// now copy the FP registers
			simple_memcpy(saved_context->fp_registers, new_context->fp_registers, sizeof(saved_context->fp_registers));
		#endif
	}

out:
	if (thread) {
		fthread_release(thread);
	}
	return status;
};
