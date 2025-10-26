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
#include <ferro/core/cpu.private.h>

// ONLY FOR TESTING
#include <ferro/core/console.h>

// used by our helpers
void farch_uthread_set_interrupt_disable_count(uint64_t idc) {
	FARCH_PER_CPU(outstanding_interrupt_disable_count) = idc;
};

FERRO_NO_RETURN void farch_uthread_jump_user_frame(void* rip, void* rsp);

void futhread_jump_user_self_arch(fthread_t* uthread, futhread_data_t* udata, void* address) {
	// don't want to be interrupted while we're switching important registers (esp. not while doing `swapgs`)
	fint_disable();

	// make sure this CPU is ready to handle the thread
	futhread_arch_ensure_ready_cpu();

	farch_uthread_jump_user_frame(address, (char*)udata->user_stack_base + udata->user_stack_size);
};

void futhread_ending_interrupt_arch(fthread_t* uthread, futhread_data_t* udata) {
	futhread_data_private_t* private_data = (void*)udata;
	FARCH_PER_CPU(current_uthread_data) = udata;
	farch_msr_write(farch_msr_fs_base, private_data->arch.fs_base);
	// see syscalls/thread_set_gs.c for the reason why we set gs_base_kernel instead of gs_base
	farch_msr_write(farch_msr_gs_base_kernel, private_data->arch.gs_base);

	// we may be on another CPU than the one this thread was running on previously;
	// make sure we're ready to handle userspace
	futhread_arch_ensure_ready_cpu();
};

void farch_uthread_syscall_handler_wrapper();

static void log_context(fthread_saved_context_t* context) {
	fconsole_logf(
		"rax=" FERRO_U64_FORMAT "; rcx=" FERRO_U64_FORMAT "\n"
		"rdx=" FERRO_U64_FORMAT "; rbx=" FERRO_U64_FORMAT "\n"
		"rsi=" FERRO_U64_FORMAT "; rdi=" FERRO_U64_FORMAT "\n"
		"rsp=" FERRO_U64_FORMAT "; rbp=" FERRO_U64_FORMAT "\n"
		"r8=" FERRO_U64_FORMAT "; r9=" FERRO_U64_FORMAT "\n"
		"r10=" FERRO_U64_FORMAT "; r11=" FERRO_U64_FORMAT "\n"
		"r12=" FERRO_U64_FORMAT "; r13=" FERRO_U64_FORMAT "\n"
		"r14=" FERRO_U64_FORMAT "; r15=" FERRO_U64_FORMAT "\n"
		"rip=" FERRO_U64_FORMAT "; rflags=" FERRO_U64_FORMAT "\n"
		"cs=" FERRO_U64_FORMAT "; ss=" FERRO_U64_FORMAT "\n"
		"interrupt_disable=" FERRO_U64_FORMAT "\n"
		"address_space=" FERRO_U64_FORMAT "\n",
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

FERRO_NO_RETURN
extern void farch_uthread_syscall_exit_preserve_all(const fthread_saved_context_t* context);

void farch_uthread_syscall_handler(void) {
	futhread_data_private_t* private_data = (void*)FARCH_PER_CPU(current_uthread_data);

	// syscalls mask out the interrupt flag, so interrupts are disabled right now
	FARCH_PER_CPU(outstanding_interrupt_disable_count) = 1;

	// but we want them to be enabled for the syscall handling because we're not actually in an interrupt;
	// we're executing in a kernel-space thread context
	fint_enable();

	// we know that, coming from userspace, we have no reason to be marked as interrupted;
	// any possible signals will be checked in a moment anyways. the only time we care about the
	// thread interrupt flag is *during* a syscall, since it lets us know that we should exit early.
	fthread_unmark_interrupted(FARCH_PER_CPU(current_thread));

	if (futhread_handle_signals(FARCH_PER_CPU(current_thread), false) != ferr_signaled) {
		if (FARCH_PER_CPU(current_uthread_data)->syscall_handler) {
			FARCH_PER_CPU(current_uthread_data)->syscall_handler(FARCH_PER_CPU(current_uthread_data)->syscall_handler_context, FARCH_PER_CPU(current_thread), FARCH_PER_CPU(current_uthread_data)->saved_syscall_context);
		} else {
			// TODO: indicate that the thread is dying from an error
			fthread_kill_self();
		}
	}

	flock_mutex_lock(&private_data->signals_mutex);

	// if there are signals to handle, it'll set them up to be handled upon return to userspace.
	FERRO_WUR_IGNORE(futhread_handle_signals(FARCH_PER_CPU(current_thread), true));

	// since we're heading back into userspace, we want to disable interrupts for the context switching (to avoid corrupting the processor state)
	fint_disable();

	// we unlock this with interrupts disabled to avoid a race if someone else signals us with a preemptive signal
	// and sees that we're in kernel-space. if they see we're in kernel-space, they just queue the preemptive signal.
	// if we unlocked this with interrupts enabled, someone might signal us in the time between the check we just did and
	// the interrupt-disable.
	flock_mutex_unlock(&private_data->signals_mutex);

	// we can also unmark the thread as interrupted here.
	// we know that if someone set the "interrupted" flag, that's because
	// a signal was pending, which we've already handled.
	fthread_unmark_interrupted(FARCH_PER_CPU(current_thread));

	if (private_data->use_fake_interrupt_return) {
		private_data->use_fake_interrupt_return = false;
		farch_uthread_syscall_exit_preserve_all(FARCH_PER_CPU(current_uthread_data)->saved_syscall_context);
	}
};

void futhread_arch_ensure_ready_cpu(void) {
	// disable interrupts to prevent this thread from being migrated to another CPU
	// TODO: introduce a proper way to pin threads to CPUs
	fint_disable();

	if (fcpu_current()->flags & farch_cpu_flag_userspace_ready) {
		fint_enable();
		return;
	}

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

	// mark this CPU as userspace-ready
	fcpu_current()->flags |= farch_cpu_flag_userspace_ready;

	fint_enable();
};

void futhread_arch_init(void) {
	// nothing for now
};

void futhread_arch_init_private_data(futhread_data_private_t* data) {
	data->arch.fs_base = 0;
	data->arch.gs_base = 0;
};
