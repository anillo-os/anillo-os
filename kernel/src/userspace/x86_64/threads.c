/*
 * This file is part of Anillo OS
 * Copyright (C) 2021 Anillo OS Developers
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

#include <ferro/userspace/threads.private.h>
#include <ferro/core/interrupts.h>
#include <libsimple/libsimple.h>
#include <ferro/core/x86_64/msr.h>

// ONLY FOR TESTING
#include <ferro/core/console.h>

// used by our helpers
void farch_uthread_set_interrupt_disable_count(uint64_t idc) {
	FARCH_PER_CPU(outstanding_interrupt_disable_count) = idc;
};

FERRO_NO_RETURN void farch_uthread_jump_user_frame(void* rip, void* rsp);

void futhread_jump_user_self_arch(fthread_t* uthread, futhread_data_t* udata, void* address) {
	farch_uthread_jump_user_frame(address, (char*)udata->user_stack_base + udata->user_stack_size);
};

void futhread_ending_interrupt_arch(fthread_t* uthread, futhread_data_t* udata) {
	futhread_data_private_t* private_data = (void*)udata;
	FARCH_PER_CPU(current_uthread_data) = udata;
	farch_msr_write(farch_msr_fs_base, private_data->arch.fs_base);
	// see syscalls/thread_set_gs.c for the reason why we set gs_base_kernel instead of gs_base
	farch_msr_write(farch_msr_gs_base_kernel, private_data->arch.gs_base);
};

void farch_uthread_syscall_handler_wrapper();

static void log_context(fthread_saved_context_t* context) {
	fconsole_logf(
		"rax=%llu; rcx=%llu\n"
		"rdx=%llu; rbx=%llu\n"
		"rsi=%llu; rdi=%llu\n"
		"rsp=%llu; rbp=%llu\n"
		"r8=%llu; r9=%llu\n"
		"r10=%llu; r11=%llu\n"
		"r12=%llu; r13=%llu\n"
		"r14=%llu; r15=%llu\n"
		"rip=%llu; rflags=%llu\n"
		"cs=%llu; ss=%llu\n"
		"interrupt_disable=%llu\n"
		"address_space=%llu\n",
		context->rax, context->rcx,
		context->rdx, context->rbx,
		context->rsi, context->rdi,
		context->rsp, context->rbp,
		context->r8, context->r9,
		context->r10, context->r11,
		context->r12, context->r13,
		context->r14, context->r15,
		context->rip, context->rflags,
		context->cs, context->ss,
		context->interrupt_disable,
		context->address_space
	);
};

void farch_uthread_syscall_handler(void) {
	// syscalls mask out the interrupt flag, so interrupts are disabled right now
	FARCH_PER_CPU(outstanding_interrupt_disable_count) = 1;

	// but we want them to be enabled for the syscall handling because we're not actually in an interrupt;
	// we're executing in a kernel-space thread context
	fint_enable();

	if (FARCH_PER_CPU(current_uthread_data)->syscall_handler) {
		FARCH_PER_CPU(current_uthread_data)->syscall_handler(FARCH_PER_CPU(current_uthread_data)->syscall_handler_context, FARCH_PER_CPU(current_thread), &FARCH_PER_CPU(current_uthread_data)->saved_syscall_context);
	} else {
		// TODO: indicate that the thread is dying from an error
		fthread_kill_self();
	}

	// since we're heading back into userspace, we want to disable interrupts for the context switching (to avoid corrupting the processor state)
	fint_disable();
};

void futhread_arch_init(void) {
	uint64_t star = 0;

	// sysret cs and ss
	// the cs is this value + 16
	// the ss is this value + 8
	star |= (((uint64_t)(farch_int_gdt_index_data_user - 1) * 8)) << 48;

	// syscall cs and ss
	// the cs is this value
	// the ss is this value + 8
	star |= ((uint64_t)farch_int_gdt_index_code * 8) << 32;

	// leave the 32-bit STAR target EIP as 0

	// write the STAR register
	farch_msr_write(farch_msr_star, star);

	// write the LSTAR register with our syscall handler
	farch_msr_write(farch_msr_lstar, (uintptr_t)farch_uthread_syscall_handler_wrapper);

	// clear the CSTAR register (so that compatiblity mode doesn't work)
	farch_msr_write(farch_msr_cstar, 0);

	// set the SFMASK register to clear every flag except the always-one flag
	// (this means interrupts will be disabled when entering a syscall)
	farch_msr_write(farch_msr_sfmask, ~(1ULL << 1));

	// enable SCE (System Call Extensions) in the EFER (Extended Feature Enable Register)
	farch_msr_write(farch_msr_efer, farch_msr_read(farch_msr_efer) | (1ULL << 0));
};

void futhread_arch_init_private_data(futhread_data_private_t* data) {
	data->arch.fs_base = 0;
	data->arch.gs_base = 0;
};
