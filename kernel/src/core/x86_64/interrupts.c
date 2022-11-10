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

/**
 * @file
 *
 * x86_64 interrupt handling.
 *
 * This file also implements facpi_reboot_early().
 */

#include <ferro/core/interrupts.h>
#include <ferro/core/panic.h>
#include <ferro/core/paging.h>
#include <ferro/core/console.h>
#include <ferro/core/locks.h>
#include <libsimple/libsimple.h>
#include <ferro/core/threads.private.h>
#include <ferro/core/per-cpu.private.h>
#include <ferro/core/x86_64/xsave.h>
#include <ferro/core/cpu.private.h>

#include <stddef.h>

#define IST_STACK_PAGE_COUNT FPAGE_LARGE_PAGE_COUNT

#ifndef FINT_DEBUG_LOG_INTERRUPTS
	#define FINT_DEBUG_LOG_INTERRUPTS 1
#endif

typedef void (*fint_isr_f)(fint_frame_t* frame);
typedef void (*fint_isr_with_code_f)(fint_frame_t* frame);
typedef FERRO_NO_RETURN void (*fint_isr_noreturn_f)(fint_frame_t* frame);
typedef FERRO_NO_RETURN void (*fint_isr_with_code_noreturn_f)(fint_frame_t* frame);

FERRO_STRUCT(fint_handler_common_data) {
	char reserved;
};

FERRO_OPTIONS(uint64_t, fint_handler_flags) {
	fint_handler_flag_safe_mode = 1 << 0,
};

FERRO_STRUCT(fint_handler_entry) {
	fint_handler_flags_t flags;
	farch_int_handler_f handler;
	void* data;
	flock_spin_intsafe_t lock;
};

FERRO_STRUCT(fint_special_handler_entry) {
	fint_special_handler_f handler;
	void* data;
	flock_spin_intsafe_t lock;
};

static farch_int_idt_t idt = {0};
static fint_handler_entry_t handlers[224] = {0};

#define SPECIAL_HANDLERS_MAX fint_special_interrupt_common_LAST
static fint_special_handler_entry_t special_handlers[SPECIAL_HANDLERS_MAX] = {
	[0 ... (SPECIAL_HANDLERS_MAX - 1)] = {
		.data = NULL,
		.handler = NULL,
		.lock = FLOCK_SPIN_INTSAFE_INIT,
	}
};

static void fint_handler_common_begin(fint_handler_common_data_t* data, fint_frame_t* frame, bool safe_mode) {
	// for all our handlers, we set a bit in their configuration to tell the CPU to disable interrupts when handling them
	// so we need to let our interrupt management code know this
	frame->saved_registers.interrupt_disable = FARCH_PER_CPU(outstanding_interrupt_disable_count);
	FARCH_PER_CPU(outstanding_interrupt_disable_count) = 1;

	// we also need to set the current interrupt frame
	frame->previous_frame = FARCH_PER_CPU(current_exception_frame);
	if (frame->previous_frame == frame) {
		fpanic("Bad frame");
	}
	FARCH_PER_CPU(current_exception_frame) = frame;

	// we also need to save the current address space
	frame->saved_registers.address_space = (uintptr_t)FARCH_PER_CPU(address_space);

	if (!safe_mode && FARCH_PER_CPU(current_thread)) {
		fthread_interrupt_start(FARCH_PER_CPU(current_thread));
	}
};

static void fint_handler_common_end(fint_handler_common_data_t* data, fint_frame_t* frame, bool safe_mode) {
	if (FARCH_PER_CPU(current_thread)) {
		// HACK: this assumes that the ending-interrupt hooks should not be doing anything expensive or dangerous.
		//       currently, we only have two ending-interrupt hooks: one for scheduler (which marks threads as running)
		//       and one for uthreads (which sets the per-thread data pointer).
		fthread_interrupt_end(FARCH_PER_CPU(current_thread));
	}

	fpanic_status(fpage_space_swap((void*)frame->saved_registers.address_space));

	FARCH_PER_CPU(current_exception_frame) = frame->previous_frame;
	FARCH_PER_CPU(outstanding_interrupt_disable_count) = frame->saved_registers.interrupt_disable;
};

static void print_frame(const fint_frame_t* frame) {
	fconsole_logf(
		"rip=%p; rsp=%p\n"
		"rax=%llu; rcx=%llu\n"
		"rdx=%llu; rbx=%llu\n"
		"rsi=%llu; rdi=%llu\n"
		"rbp=%llu; r8=%llu\n"
		"r9=%llu; r10=%llu\n"
		"r11=%llu; r12=%llu\n"
		"r13=%llu; r14=%llu\n"
		"r15=%llu; rflags=%llu\n"
		"cs=%llu; ss=%llu\n"
		"ds=%u; es=%u\n"
		"fs=%u; gs=%u\n"
		,
		(void*)((uintptr_t)frame->core.rip - 1), frame->core.rsp,
		frame->saved_registers.rax, frame->saved_registers.rcx,
		frame->saved_registers.rdx, frame->saved_registers.rbx,
		frame->saved_registers.rsi, frame->saved_registers.rdi,
		frame->saved_registers.rbp, frame->saved_registers.r8,
		frame->saved_registers.r9, frame->saved_registers.r10,
		frame->saved_registers.r11, frame->saved_registers.r12,
		frame->saved_registers.r13, frame->saved_registers.r14,
		frame->saved_registers.r15, frame->core.rflags,
		frame->core.cs, frame->core.ss,
		frame->saved_registers.ds, frame->saved_registers.es,
		frame->saved_registers.fs, frame->saved_registers.gs
	);
};

FERRO_PACKED_STRUCT(fint_stack_frame) {
	fint_stack_frame_t* previous_frame;
	void* return_address;
};

static void trace_stack(const fint_stack_frame_t* frame) {
	fconsole_log("stack trace:\n");
	for (size_t i = 0; i < 20; ++i) {
		if (
			// if we can't find it in the kernel address space AND
			fpage_virtual_to_physical((uintptr_t)frame) == UINTPTR_MAX &&
			// we can't find it in the active address space (if we have one at all)
			fpage_space_virtual_to_physical(fpage_space_current(), (uintptr_t)frame) == UINTPTR_MAX
		) {
			// then this is an invalid address. stop the stack trace here.
			break;
		}

		fconsole_logf("%p\n", frame->return_address);

		frame = frame->previous_frame;
	}
};

FERRO_OPTIONS(uint64_t, farch_int_page_fault_code_flags) {
	farch_int_page_fault_code_flag_protection        = 1ULL << 0,
	farch_int_page_fault_code_flag_write             = 1ULL << 1,
	farch_int_page_fault_code_flag_user              = 1ULL << 2,
	farch_int_page_fault_code_flag_reserved          = 1ULL << 3,
	farch_int_page_fault_code_flag_instruction_fetch = 1ULL << 4,

	farch_int_page_fault_code_flags_all = farch_int_page_fault_code_flag_protection        |
	                                      farch_int_page_fault_code_flag_write             |
	                                      farch_int_page_fault_code_flag_user              |
	                                      farch_int_page_fault_code_flag_reserved          |
	                                      farch_int_page_fault_code_flag_instruction_fetch
};

void fint_log_frame(const fint_frame_t* frame) {
	print_frame(frame);
};

void fint_trace_interrupted_stack(const fint_frame_t* frame) {
	trace_stack((void*)frame->saved_registers.rbp);
};

void fint_trace_current_stack(void) {
	uint64_t rbp;
	__asm__ volatile("mov %%rbp, %0" : "=r" (rbp));
	trace_stack((void*)rbp);
};

static void print_page_fault_code(uint64_t page_fault_code) {
	bool is_first = true;

	for (uint8_t bit = 0; bit < 64; ++bit) {
		const char* message = NULL;
		bool set = (page_fault_code & (1ULL << bit)) != 0;

		if ((farch_int_page_fault_code_flags_all & (1ULL << bit)) == 0) {
			continue;
		}

		switch (bit) {
			case 0:
				message = set ? "protection violation" : "missing page";
				break;
			case 1:
				message = set ? "caused by write" : "caused by read";
				break;
			case 2:
				message = set ? "occurred in userspace" : "occurred in kernel-space";
				break;
			case 3:
				message = set ? "invalid page descriptor (reserved bit set)" : NULL;
				break;
			case 4:
				message = set ? "caused by instruction fetch" : NULL;
				break;
		}

		if (message) {
			if (is_first) {
				is_first = false;
			} else {
				fconsole_log(" | ");
			}
			fconsole_log(message);
		}
	}
};

#define INTERRUPT_HANDLER(name) \
	void farch_int_wrapper_ ## name(void); \
	void farch_int_ ## name ## _handler(fint_frame_t* frame)

#define INTERRUPT_HANDLER_NORETURN(name) \
	FERRO_NO_RETURN void farch_int_wrapper_ ## name(void); \
	FERRO_NO_RETURN void farch_int_ ## name ## _handler(fint_frame_t* frame)

INTERRUPT_HANDLER(debug) {
	fint_handler_common_data_t data;
	uint64_t dr6;
	fint_special_handler_entry_t* entry = NULL;
	fint_special_handler_f handler = NULL;
	void* handler_data = NULL;

	__asm__ volatile("mov %%dr6, %0" : "=r" (dr6));

	fint_handler_common_begin(&data, frame, true);

	if ((dr6 & (1ULL << 14)) != 0) {
		dr6 &= ~(1ULL << 14);
		entry = &special_handlers[fint_special_interrupt_common_single_step];
		frame->core.rflags &= ~(1ULL << 8);
	} else if ((dr6 & 0x0fULL) != 0) {
		dr6 &= ~(0x0fULL);
		entry = &special_handlers[fint_special_interrupt_common_watchpoint];
	}

	__asm__ volatile("mov %0, %%dr6" :: "r" (dr6));

	if (entry) {
		flock_spin_intsafe_lock(&entry->lock);
		handler = entry->handler;
		handler_data = entry->data;
		flock_spin_intsafe_unlock(&entry->lock);
	}

	if (handler) {
		handler(handler_data);
	} else {
		fconsole_logf("watchpoint hit; frame:\n");
		print_frame(frame);
	}

	fint_handler_common_end(&data, frame, true);
};

INTERRUPT_HANDLER(breakpoint) {
	fint_handler_common_data_t data;
	fint_special_handler_entry_t* entry = &special_handlers[fint_special_interrupt_common_breakpoint];
	fint_special_handler_f handler = NULL;
	void* handler_data = NULL;

	fint_handler_common_begin(&data, frame, true);

	flock_spin_intsafe_lock(&entry->lock);
	handler = entry->handler;
	handler_data = entry->data;
	flock_spin_intsafe_unlock(&entry->lock);

	if (handler) {
		frame->core.rip = (void*)((uintptr_t)frame->core.rip - 1);
		handler(handler_data);
	} else {
		fconsole_logf("breakpoint hit; frame:\n");
		print_frame(frame);
		trace_stack((void*)frame->saved_registers.rbp);
	}

	fint_handler_common_end(&data, frame, true);
};

INTERRUPT_HANDLER_NORETURN(double_fault) {
	fint_handler_common_data_t data;

	fint_handler_common_begin(&data, frame, true);

	fconsole_logf("double faulted; going down now; code=%llu; frame:\n", frame->code);
	print_frame(frame);
	trace_stack((void*)frame->saved_registers.rbp);
	fpanic("double fault");

	// unnecessary, but just for consistency
	fint_handler_common_end(&data, frame, true);
};

INTERRUPT_HANDLER(general_protection) {
	fint_handler_common_data_t data;
	bool handled = false;

	fint_handler_common_begin(&data, frame, true);

	if (fint_current_frame() == fint_root_frame(fint_current_frame()) && FARCH_PER_CPU(current_thread)) {
		fthread_t* thread = FARCH_PER_CPU(current_thread);
		fthread_private_t* private_thread = (void*)thread;
		uint8_t hooks_in_use;

		flock_spin_intsafe_lock(&thread->lock);
		hooks_in_use = private_thread->hooks_in_use;
		flock_spin_intsafe_unlock(&thread->lock);

		for (uint8_t slot = 0; slot < sizeof(private_thread->hooks) / sizeof(*private_thread->hooks); ++slot) {
			if ((hooks_in_use & (1 << slot)) == 0) {
				continue;
			}

			if (!private_thread->hooks[slot].illegal_instruction) {
				continue;
			}

			ferr_t hook_status = private_thread->hooks[slot].illegal_instruction(private_thread->hooks[slot].context, thread);

			if (hook_status == ferr_ok || hook_status == ferr_permanent_outage) {
				handled = true;
			}

			if (hook_status == ferr_permanent_outage) {
				break;
			}
		}
	}

	if (!handled) {
		fconsole_logf("general protection fault; code=%llu; frame:\n", frame->code);
		print_frame(frame);
		trace_stack((void*)frame->saved_registers.rbp);
		fpanic("general protection fault");
	}

	fint_handler_common_end(&data, frame, true);
};

INTERRUPT_HANDLER(page_fault) {
	fint_handler_common_data_t data;
	uintptr_t faulting_address = 0;
	fint_special_handler_entry_t* entry = &special_handlers[fint_special_interrupt_page_fault];
	fint_special_handler_f handler = NULL;
	void* handler_data = NULL;

	fint_handler_common_begin(&data, frame, true);

	__asm__ volatile("mov %%cr2, %0" : "=r" (faulting_address));

	if (entry) {
		flock_spin_intsafe_lock(&entry->lock);
		handler = entry->handler;
		handler_data = entry->data;
		flock_spin_intsafe_unlock(&entry->lock);
	}

	if (handler) {
		handler(handler_data);
	} else {
		fconsole_logf("page fault; code=%llu; faulting address=%p; frame:\n", frame->code, (void*)faulting_address);
		fconsole_log("page fault code description: ");
		print_page_fault_code(frame->code);
		fconsole_log("\n");
		print_frame(frame);
		trace_stack((void*)frame->saved_registers.rbp);
		fpanic("page fault");
	}

	fint_handler_common_end(&data, frame, true);
};

INTERRUPT_HANDLER(invalid_opcode) {
	fint_handler_common_data_t data;
	fint_special_handler_entry_t* entry = &special_handlers[fint_special_interrupt_invalid_instruction];
	fint_special_handler_f handler = NULL;
	void* handler_data = NULL;

	fint_handler_common_begin(&data, frame, true);

	if (entry) {
		flock_spin_intsafe_lock(&entry->lock);
		handler = entry->handler;
		handler_data = entry->data;
		flock_spin_intsafe_unlock(&entry->lock);
	}

	if (handler) {
		handler(handler_data);
	} else {
		fconsole_logf("invalid opcode; frame:\n");
		print_frame(frame);
		trace_stack((void*)frame->saved_registers.rbp);
		fpanic("invalid opcode");
	}

	fint_handler_common_end(&data, frame, true);
};

INTERRUPT_HANDLER(simd_exception) {
	fint_handler_common_data_t data;
	uint64_t mxcsr = ((farch_xsave_area_legacy_t*)frame->xsave_area)->mxcsr;
	farch_xsave_header_t* xsave_header = (void*)((char*)frame->xsave_area + 512);
	bool handled = false;

	fint_handler_common_begin(&data, frame, true);

	if (fint_current_frame() == fint_root_frame(fint_current_frame()) && FARCH_PER_CPU(current_thread)) {
		fthread_t* thread = FARCH_PER_CPU(current_thread);
		fthread_private_t* private_thread = (void*)thread;
		uint8_t hooks_in_use;

		flock_spin_intsafe_lock(&thread->lock);
		hooks_in_use = private_thread->hooks_in_use;
		flock_spin_intsafe_unlock(&thread->lock);

		for (uint8_t slot = 0; slot < sizeof(private_thread->hooks) / sizeof(*private_thread->hooks); ++slot) {
			if ((hooks_in_use & (1 << slot)) == 0) {
				continue;
			}

			if (!private_thread->hooks[slot].floating_point_exception) {
				continue;
			}

			ferr_t hook_status = private_thread->hooks[slot].floating_point_exception(private_thread->hooks[slot].context, thread);

			if (hook_status == ferr_ok || hook_status == ferr_permanent_outage) {
				handled = true;
			}

			if (hook_status == ferr_permanent_outage) {
				break;
			}
		}
	}

	if (!handled) {
		fconsole_logf("simd exception with MXCSR=%llu, XSTATE_BV=%llu, XCOMP_BV=%llu; frame:\n", mxcsr, xsave_header->xstate_bv, xsave_header->xcomp_bv);
		fint_log_frame(frame);
		fint_trace_interrupted_stack(frame);
		fpanic("simd exception");
	}

	fint_handler_common_end(&data, frame, true);
};

#define MISC_INTERRUPT_HANDLER(number) \
	INTERRUPT_HANDLER(interrupt_ ## number) { \
		fint_handler_common_data_t data; \
		farch_int_handler_f handler = NULL; \
		void* handler_data = NULL; \
		bool safe_mode = /* this is racy! */ (handlers[number].flags & fint_handler_flag_safe_mode) != 0; \
		fint_handler_common_begin(&data, frame, safe_mode); \
		flock_spin_intsafe_lock(&handlers[number].lock); \
		handler = handlers[number].handler; \
		handler_data = handlers[number].data; \
		flock_spin_intsafe_unlock(&handlers[number].lock); \
		if (handler) { \
			handler(handler_data, frame); \
		} else { \
			fpanic("Unhandled interrupt " #number); \
		} \
		fint_handler_common_end(&data, frame, safe_mode); \
	};

MISC_INTERRUPT_HANDLER(  0);
MISC_INTERRUPT_HANDLER(  1);
MISC_INTERRUPT_HANDLER(  2);
MISC_INTERRUPT_HANDLER(  3);
MISC_INTERRUPT_HANDLER(  4);
MISC_INTERRUPT_HANDLER(  5);
MISC_INTERRUPT_HANDLER(  6);
MISC_INTERRUPT_HANDLER(  7);
MISC_INTERRUPT_HANDLER(  8);
MISC_INTERRUPT_HANDLER(  9);
MISC_INTERRUPT_HANDLER( 10);
MISC_INTERRUPT_HANDLER( 11);
MISC_INTERRUPT_HANDLER( 12);
MISC_INTERRUPT_HANDLER( 13);
MISC_INTERRUPT_HANDLER( 14);
MISC_INTERRUPT_HANDLER( 15);
MISC_INTERRUPT_HANDLER( 16);
MISC_INTERRUPT_HANDLER( 17);
MISC_INTERRUPT_HANDLER( 18);
MISC_INTERRUPT_HANDLER( 19);
MISC_INTERRUPT_HANDLER( 20);
MISC_INTERRUPT_HANDLER( 21);
MISC_INTERRUPT_HANDLER( 22);
MISC_INTERRUPT_HANDLER( 23);
MISC_INTERRUPT_HANDLER( 24);
MISC_INTERRUPT_HANDLER( 25);
MISC_INTERRUPT_HANDLER( 26);
MISC_INTERRUPT_HANDLER( 27);
MISC_INTERRUPT_HANDLER( 28);
MISC_INTERRUPT_HANDLER( 29);
MISC_INTERRUPT_HANDLER( 30);
MISC_INTERRUPT_HANDLER( 31);
MISC_INTERRUPT_HANDLER( 32);
MISC_INTERRUPT_HANDLER( 33);
MISC_INTERRUPT_HANDLER( 34);
MISC_INTERRUPT_HANDLER( 35);
MISC_INTERRUPT_HANDLER( 36);
MISC_INTERRUPT_HANDLER( 37);
MISC_INTERRUPT_HANDLER( 38);
MISC_INTERRUPT_HANDLER( 39);
MISC_INTERRUPT_HANDLER( 40);
MISC_INTERRUPT_HANDLER( 41);
MISC_INTERRUPT_HANDLER( 42);
MISC_INTERRUPT_HANDLER( 43);
MISC_INTERRUPT_HANDLER( 44);
MISC_INTERRUPT_HANDLER( 45);
MISC_INTERRUPT_HANDLER( 46);
MISC_INTERRUPT_HANDLER( 47);
MISC_INTERRUPT_HANDLER( 48);
MISC_INTERRUPT_HANDLER( 49);
MISC_INTERRUPT_HANDLER( 50);
MISC_INTERRUPT_HANDLER( 51);
MISC_INTERRUPT_HANDLER( 52);
MISC_INTERRUPT_HANDLER( 53);
MISC_INTERRUPT_HANDLER( 54);
MISC_INTERRUPT_HANDLER( 55);
MISC_INTERRUPT_HANDLER( 56);
MISC_INTERRUPT_HANDLER( 57);
MISC_INTERRUPT_HANDLER( 58);
MISC_INTERRUPT_HANDLER( 59);
MISC_INTERRUPT_HANDLER( 60);
MISC_INTERRUPT_HANDLER( 61);
MISC_INTERRUPT_HANDLER( 62);
MISC_INTERRUPT_HANDLER( 63);
MISC_INTERRUPT_HANDLER( 64);
MISC_INTERRUPT_HANDLER( 65);
MISC_INTERRUPT_HANDLER( 66);
MISC_INTERRUPT_HANDLER( 67);
MISC_INTERRUPT_HANDLER( 68);
MISC_INTERRUPT_HANDLER( 69);
MISC_INTERRUPT_HANDLER( 70);
MISC_INTERRUPT_HANDLER( 71);
MISC_INTERRUPT_HANDLER( 72);
MISC_INTERRUPT_HANDLER( 73);
MISC_INTERRUPT_HANDLER( 74);
MISC_INTERRUPT_HANDLER( 75);
MISC_INTERRUPT_HANDLER( 76);
MISC_INTERRUPT_HANDLER( 77);
MISC_INTERRUPT_HANDLER( 78);
MISC_INTERRUPT_HANDLER( 79);
MISC_INTERRUPT_HANDLER( 80);
MISC_INTERRUPT_HANDLER( 81);
MISC_INTERRUPT_HANDLER( 82);
MISC_INTERRUPT_HANDLER( 83);
MISC_INTERRUPT_HANDLER( 84);
MISC_INTERRUPT_HANDLER( 85);
MISC_INTERRUPT_HANDLER( 86);
MISC_INTERRUPT_HANDLER( 87);
MISC_INTERRUPT_HANDLER( 88);
MISC_INTERRUPT_HANDLER( 89);
MISC_INTERRUPT_HANDLER( 90);
MISC_INTERRUPT_HANDLER( 91);
MISC_INTERRUPT_HANDLER( 92);
MISC_INTERRUPT_HANDLER( 93);
MISC_INTERRUPT_HANDLER( 94);
MISC_INTERRUPT_HANDLER( 95);
MISC_INTERRUPT_HANDLER( 96);
MISC_INTERRUPT_HANDLER( 97);
MISC_INTERRUPT_HANDLER( 98);
MISC_INTERRUPT_HANDLER( 99);
MISC_INTERRUPT_HANDLER(100);
MISC_INTERRUPT_HANDLER(101);
MISC_INTERRUPT_HANDLER(102);
MISC_INTERRUPT_HANDLER(103);
MISC_INTERRUPT_HANDLER(104);
MISC_INTERRUPT_HANDLER(105);
MISC_INTERRUPT_HANDLER(106);
MISC_INTERRUPT_HANDLER(107);
MISC_INTERRUPT_HANDLER(108);
MISC_INTERRUPT_HANDLER(109);
MISC_INTERRUPT_HANDLER(110);
MISC_INTERRUPT_HANDLER(111);
MISC_INTERRUPT_HANDLER(112);
MISC_INTERRUPT_HANDLER(113);
MISC_INTERRUPT_HANDLER(114);
MISC_INTERRUPT_HANDLER(115);
MISC_INTERRUPT_HANDLER(116);
MISC_INTERRUPT_HANDLER(117);
MISC_INTERRUPT_HANDLER(118);
MISC_INTERRUPT_HANDLER(119);
MISC_INTERRUPT_HANDLER(120);
MISC_INTERRUPT_HANDLER(121);
MISC_INTERRUPT_HANDLER(122);
MISC_INTERRUPT_HANDLER(123);
MISC_INTERRUPT_HANDLER(124);
MISC_INTERRUPT_HANDLER(125);
MISC_INTERRUPT_HANDLER(126);
MISC_INTERRUPT_HANDLER(127);
MISC_INTERRUPT_HANDLER(128);
MISC_INTERRUPT_HANDLER(129);
MISC_INTERRUPT_HANDLER(130);
MISC_INTERRUPT_HANDLER(131);
MISC_INTERRUPT_HANDLER(132);
MISC_INTERRUPT_HANDLER(133);
MISC_INTERRUPT_HANDLER(134);
MISC_INTERRUPT_HANDLER(135);
MISC_INTERRUPT_HANDLER(136);
MISC_INTERRUPT_HANDLER(137);
MISC_INTERRUPT_HANDLER(138);
MISC_INTERRUPT_HANDLER(139);
MISC_INTERRUPT_HANDLER(140);
MISC_INTERRUPT_HANDLER(141);
MISC_INTERRUPT_HANDLER(142);
MISC_INTERRUPT_HANDLER(143);
MISC_INTERRUPT_HANDLER(144);
MISC_INTERRUPT_HANDLER(145);
MISC_INTERRUPT_HANDLER(146);
MISC_INTERRUPT_HANDLER(147);
MISC_INTERRUPT_HANDLER(148);
MISC_INTERRUPT_HANDLER(149);
MISC_INTERRUPT_HANDLER(150);
MISC_INTERRUPT_HANDLER(151);
MISC_INTERRUPT_HANDLER(152);
MISC_INTERRUPT_HANDLER(153);
MISC_INTERRUPT_HANDLER(154);
MISC_INTERRUPT_HANDLER(155);
MISC_INTERRUPT_HANDLER(156);
MISC_INTERRUPT_HANDLER(157);
MISC_INTERRUPT_HANDLER(158);
MISC_INTERRUPT_HANDLER(159);
MISC_INTERRUPT_HANDLER(160);
MISC_INTERRUPT_HANDLER(161);
MISC_INTERRUPT_HANDLER(162);
MISC_INTERRUPT_HANDLER(163);
MISC_INTERRUPT_HANDLER(164);
MISC_INTERRUPT_HANDLER(165);
MISC_INTERRUPT_HANDLER(166);
MISC_INTERRUPT_HANDLER(167);
MISC_INTERRUPT_HANDLER(168);
MISC_INTERRUPT_HANDLER(169);
MISC_INTERRUPT_HANDLER(170);
MISC_INTERRUPT_HANDLER(171);
MISC_INTERRUPT_HANDLER(172);
MISC_INTERRUPT_HANDLER(173);
MISC_INTERRUPT_HANDLER(174);
MISC_INTERRUPT_HANDLER(175);
MISC_INTERRUPT_HANDLER(176);
MISC_INTERRUPT_HANDLER(177);
MISC_INTERRUPT_HANDLER(178);
MISC_INTERRUPT_HANDLER(179);
MISC_INTERRUPT_HANDLER(180);
MISC_INTERRUPT_HANDLER(181);
MISC_INTERRUPT_HANDLER(182);
MISC_INTERRUPT_HANDLER(183);
MISC_INTERRUPT_HANDLER(184);
MISC_INTERRUPT_HANDLER(185);
MISC_INTERRUPT_HANDLER(186);
MISC_INTERRUPT_HANDLER(187);
MISC_INTERRUPT_HANDLER(188);
MISC_INTERRUPT_HANDLER(189);
MISC_INTERRUPT_HANDLER(190);
MISC_INTERRUPT_HANDLER(191);
MISC_INTERRUPT_HANDLER(192);
MISC_INTERRUPT_HANDLER(193);
MISC_INTERRUPT_HANDLER(194);
MISC_INTERRUPT_HANDLER(195);
MISC_INTERRUPT_HANDLER(196);
MISC_INTERRUPT_HANDLER(197);
MISC_INTERRUPT_HANDLER(198);
MISC_INTERRUPT_HANDLER(199);
MISC_INTERRUPT_HANDLER(200);
MISC_INTERRUPT_HANDLER(201);
MISC_INTERRUPT_HANDLER(202);
MISC_INTERRUPT_HANDLER(203);
MISC_INTERRUPT_HANDLER(204);
MISC_INTERRUPT_HANDLER(205);
MISC_INTERRUPT_HANDLER(206);
MISC_INTERRUPT_HANDLER(207);
MISC_INTERRUPT_HANDLER(208);
MISC_INTERRUPT_HANDLER(209);
MISC_INTERRUPT_HANDLER(210);
MISC_INTERRUPT_HANDLER(211);
MISC_INTERRUPT_HANDLER(212);
MISC_INTERRUPT_HANDLER(213);
MISC_INTERRUPT_HANDLER(214);
MISC_INTERRUPT_HANDLER(215);
MISC_INTERRUPT_HANDLER(216);
MISC_INTERRUPT_HANDLER(217);
MISC_INTERRUPT_HANDLER(218);
MISC_INTERRUPT_HANDLER(219);
MISC_INTERRUPT_HANDLER(220);
MISC_INTERRUPT_HANDLER(221);
MISC_INTERRUPT_HANDLER(222);
MISC_INTERRUPT_HANDLER(223);

static void fint_reload_segment_registers(uint8_t cs, uint8_t ds) {
	__asm__ volatile(
		"pushq %0\n" // set code segment for retfq
		"pushq %1\n" // set return address for retfq
		"lretq\n"    // do the retfq
		::
		"r" ((uint64_t)(cs * 8)),
		"r" ((uint64_t)&&jump_here_for_cs_reload)
		:
		"memory"
	);

jump_here_for_cs_reload:;

	__asm__ volatile(
		"mov %0, %%ss\n"
		"mov %0, %%ds\n"
		"mov %0, %%es\n"
		::
		"r" ((uint64_t)(ds * 8))
		:
		"memory"
	);
};

// NOTE: there is no chance of deadlock by holding this lock while also holding an entry lock.
//       interrupt handlers only take entry locks to read the handler data; invocation of the handlers
//       occurs with no entry locks held. therefore, handlers are free to register other handlers
//       without fear of deadlock. in non-interrupt contexts, this is also deadlock free
//       because the registration lock is always taken before any entry locks are taken and always released
//       after any entry locks are taken.
static flock_spin_intsafe_t registration_lock = FLOCK_SPIN_INTSAFE_INIT;

static ferr_t farch_int_register_handler_locked(uint8_t interrupt, farch_int_handler_f handler, void* data, farch_int_handler_flags_t flags) {
	ferr_t status = ferr_ok;
	fint_handler_entry_t* entry;

	if (interrupt < 32) {
		status = ferr_invalid_argument;
		goto out_unlocked;
	}

	entry = &handlers[interrupt - 32];

	flock_spin_intsafe_lock(&entry->lock);

	if (entry->handler) {
		status = ferr_temporary_outage;
		goto out;
	}

	entry->handler = handler;
	entry->data = data;
	entry->flags = 0;

	if (flags & farch_int_handler_flag_safe_mode) {
		entry->flags |= fint_handler_flag_safe_mode;
	}

out:
	flock_spin_intsafe_unlock(&entry->lock);
out_unlocked:
	return status;
};

ferr_t farch_int_register_handler(uint8_t interrupt, farch_int_handler_f handler, void* data, farch_int_handler_flags_t flags) {
	ferr_t status;
	flock_spin_intsafe_lock(&registration_lock);
	status = farch_int_register_handler_locked(interrupt, handler, data, flags);
	flock_spin_intsafe_unlock(&registration_lock);
	return status;
};

static ferr_t farch_int_unregister_handler_locked(uint8_t interrupt) {
	ferr_t status = ferr_ok;
	fint_handler_entry_t* entry;

	if (interrupt < 32) {
		status = ferr_invalid_argument;
		goto out_unlocked;
	}

	entry = &handlers[interrupt - 32];

	flock_spin_intsafe_lock(&entry->lock);

	if (!entry->handler) {
		status = ferr_no_such_resource;
		goto out;
	}

	entry->handler = NULL;
	entry->data = NULL;
	entry->flags = 0;

out:
	flock_spin_intsafe_unlock(&entry->lock);
out_unlocked:
	return status;
};

ferr_t farch_int_unregister_handler(uint8_t interrupt) {
	ferr_t status;
	flock_spin_intsafe_lock(&registration_lock);
	status = farch_int_unregister_handler_locked(interrupt);
	flock_spin_intsafe_unlock(&registration_lock);
	return status;
};

static uint8_t farch_int_next_available_locked(void) {
	for (size_t i = 0; i < sizeof(handlers) / sizeof(*handlers); ++i) {
		fint_handler_entry_t* entry = &handlers[i];
		bool registered = false;

		flock_spin_intsafe_lock(&entry->lock);
		registered = !!entry->handler;
		flock_spin_intsafe_unlock(&entry->lock);

		if (!registered) {
			return i + 32;
		}
	}

	return 0;
};

static uint8_t farch_int_next_available(void) {
	uint8_t result;
	flock_spin_intsafe_lock(&registration_lock);
	result = farch_int_next_available_locked();
	flock_spin_intsafe_unlock(&registration_lock);
	return result;
};

ferr_t farch_int_register_next_available(farch_int_handler_f handler, void* data, uint8_t* out_interrupt, farch_int_handler_flags_t flags) {
	ferr_t status = ferr_ok;
	uint8_t interrupt = 0;

	if (!handler || !out_interrupt) {
		status = ferr_invalid_argument;
		goto out_unlocked;
	}

	flock_spin_intsafe_lock(&registration_lock);

	interrupt = farch_int_next_available_locked();
	if (interrupt == 0) {
		status = ferr_temporary_outage;
		goto out;
	}

	status = farch_int_register_handler_locked(interrupt, handler, data, flags);

out:
	flock_spin_intsafe_unlock(&registration_lock);
out_unlocked:
	if (status == ferr_ok) {
		*out_interrupt = interrupt;
	}
	return status;
};

void fint_init(void) {
	uintptr_t tss_addr = (uintptr_t)&FARCH_PER_CPU(tss);
	void* generic_interrupt_stack_bottom = NULL;
	void* double_fault_stack_bottom = NULL;
	void* debug_stack_bottom = NULL;
	void* page_fault_stack_bottom = NULL;
	farch_int_idt_pointer_t idt_pointer;
	farch_int_gdt_pointer_t gdt_pointer;
	farch_int_idt_entry_t missing_entry;
	uint16_t tss_selector = farch_int_gdt_index_tss * 8;

	// initialize the GDT entries
	FARCH_PER_CPU(gdt).entries[farch_int_gdt_index_null] = 0;
	FARCH_PER_CPU(gdt).entries[farch_int_gdt_index_code] = farch_int_gdt_flags_common | farch_int_gdt_flag_long | farch_int_gdt_flag_executable;
	FARCH_PER_CPU(gdt).entries[farch_int_gdt_index_data] = farch_int_gdt_flags_common;
	FARCH_PER_CPU(gdt).entries[farch_int_gdt_index_tss] = farch_int_gdt_flag_accessed | farch_int_gdt_flag_executable | farch_int_gdt_flag_present | ((sizeof(farch_int_tss_t) - 1ULL) & 0xffffULL) | ((tss_addr & 0xffffffULL) << 16) | (((tss_addr & (0xffULL << 24)) >> 24) << 56);
	FARCH_PER_CPU(gdt).entries[farch_int_gdt_index_tss_other] = (tss_addr & (0xffffffffULL << 32)) >> 32;
	FARCH_PER_CPU(gdt).entries[farch_int_gdt_index_data_user] = farch_int_gdt_flags_common | farch_int_gdt_flag_dpl_ring_3;
	FARCH_PER_CPU(gdt).entries[farch_int_gdt_index_code_user] = farch_int_gdt_flags_common | farch_int_gdt_flag_long | farch_int_gdt_flag_executable | farch_int_gdt_flag_dpl_ring_3;

	// load the gdt
	gdt_pointer.limit = sizeof(FARCH_PER_CPU(gdt)) - 1;
	gdt_pointer.base = &FARCH_PER_CPU(gdt);
	__asm__ volatile(
		"lgdt (%0)"
		::
		"r" (&gdt_pointer)
		:
		"memory"
	);

	// reload the segment registers
	fint_reload_segment_registers(farch_int_gdt_index_code, farch_int_gdt_index_data);

	// load the TSS
	__asm__ volatile(
		"ltr (%0)"
		::
		"r" (&tss_selector)
		:
		"memory"
	);

	// allocate a stack for generic interrupt handlers
	if (fpage_allocate_kernel(IST_STACK_PAGE_COUNT, &generic_interrupt_stack_bottom, fpage_flag_prebound) != ferr_ok) {
		fpanic("failed to allocate stack for generic interrupt handlers");
	}

	// allocate a stack for the double-fault handler
	if (fpage_allocate_kernel(IST_STACK_PAGE_COUNT, &double_fault_stack_bottom, fpage_flag_prebound) != ferr_ok) {
		fpanic("failed to allocate stack for double fault handler");
	}

	// allocate a stack for the debug fault handler
	if (fpage_allocate_kernel(IST_STACK_PAGE_COUNT, &debug_stack_bottom, fpage_flag_prebound) != ferr_ok) {
		fpanic("failed to allocate stack for debug interrupts");
	}

	// allocate a stack for the page fault handler
	if (fpage_allocate_kernel(IST_STACK_PAGE_COUNT, &page_fault_stack_bottom, fpage_flag_prebound) != ferr_ok) {
		fpanic("failed to allocate stack for page fault interrupts");
	}

	// set the stack top addresses
	FARCH_PER_CPU(tss).ist[farch_int_ist_index_generic_interrupt] = (uintptr_t)generic_interrupt_stack_bottom + (FPAGE_PAGE_SIZE * 4);
	FARCH_PER_CPU(tss).ist[farch_int_ist_index_double_fault] = (uintptr_t)double_fault_stack_bottom + (FPAGE_PAGE_SIZE * 4);
	FARCH_PER_CPU(tss).ist[farch_int_ist_index_debug] = (uintptr_t)debug_stack_bottom + (FPAGE_PAGE_SIZE * 4);
	FARCH_PER_CPU(tss).ist[farch_int_ist_index_page_fault] = (uintptr_t)page_fault_stack_bottom + (FPAGE_PAGE_SIZE * 4);

	// initialize the idt with missing entries (they still require certain bits to be 1)
	fint_make_idt_entry(&missing_entry, NULL, 0, 0, false, 0);
	missing_entry.options &= ~farch_int_idt_entry_option_present;
	simple_memclone(&idt, &missing_entry, sizeof(missing_entry), sizeof(idt) / sizeof(missing_entry));

	// initialize the desired idt entries with actual values
	fint_make_idt_entry(&idt.debug, farch_int_wrapper_debug, farch_int_gdt_index_code, farch_int_ist_index_debug + 1, false, 0);
	fint_make_idt_entry(&idt.breakpoint, farch_int_wrapper_breakpoint, farch_int_gdt_index_code, farch_int_ist_index_generic_interrupt + 1, false, 0);
	fint_make_idt_entry(&idt.double_fault, farch_int_wrapper_double_fault, farch_int_gdt_index_code, farch_int_ist_index_double_fault + 1, false, 0);
	fint_make_idt_entry(&idt.general_protection_fault, farch_int_wrapper_general_protection, farch_int_gdt_index_code, farch_int_ist_index_generic_interrupt + 1, false, 0);
	fint_make_idt_entry(&idt.page_fault, farch_int_wrapper_page_fault, farch_int_gdt_index_code, farch_int_ist_index_page_fault + 1, false, 0);
	fint_make_idt_entry(&idt.invalid_opcode, farch_int_wrapper_invalid_opcode, farch_int_gdt_index_code, farch_int_ist_index_generic_interrupt + 1, false, 0);
	fint_make_idt_entry(&idt.simd_exception, farch_int_wrapper_simd_exception, farch_int_gdt_index_code, farch_int_ist_index_generic_interrupt + 1, false, 0);

	// initialize the array of miscellaneous interrupts
	#define DEFINE_INTERRUPT(number) \
		flock_spin_intsafe_init(&handlers[number].lock); \
		fint_make_idt_entry(&idt.interrupts[number], farch_int_wrapper_interrupt_ ## number, farch_int_gdt_index_code, farch_int_ist_index_generic_interrupt + 1, false, 0);

	DEFINE_INTERRUPT(  0);
	DEFINE_INTERRUPT(  1);
	DEFINE_INTERRUPT(  2);
	DEFINE_INTERRUPT(  3);
	DEFINE_INTERRUPT(  4);
	DEFINE_INTERRUPT(  5);
	DEFINE_INTERRUPT(  6);
	DEFINE_INTERRUPT(  7);
	DEFINE_INTERRUPT(  8);
	DEFINE_INTERRUPT(  9);
	DEFINE_INTERRUPT( 10);
	DEFINE_INTERRUPT( 11);
	DEFINE_INTERRUPT( 12);
	DEFINE_INTERRUPT( 13);
	DEFINE_INTERRUPT( 14);
	DEFINE_INTERRUPT( 15);
	DEFINE_INTERRUPT( 16);
	DEFINE_INTERRUPT( 17);
	DEFINE_INTERRUPT( 18);
	DEFINE_INTERRUPT( 19);
	DEFINE_INTERRUPT( 20);
	DEFINE_INTERRUPT( 21);
	DEFINE_INTERRUPT( 22);
	DEFINE_INTERRUPT( 23);
	DEFINE_INTERRUPT( 24);
	DEFINE_INTERRUPT( 25);
	DEFINE_INTERRUPT( 26);
	DEFINE_INTERRUPT( 27);
	DEFINE_INTERRUPT( 28);
	DEFINE_INTERRUPT( 29);
	DEFINE_INTERRUPT( 30);
	DEFINE_INTERRUPT( 31);
	DEFINE_INTERRUPT( 32);
	DEFINE_INTERRUPT( 33);
	DEFINE_INTERRUPT( 34);
	DEFINE_INTERRUPT( 35);
	DEFINE_INTERRUPT( 36);
	DEFINE_INTERRUPT( 37);
	DEFINE_INTERRUPT( 38);
	DEFINE_INTERRUPT( 39);
	DEFINE_INTERRUPT( 40);
	DEFINE_INTERRUPT( 41);
	DEFINE_INTERRUPT( 42);
	DEFINE_INTERRUPT( 43);
	DEFINE_INTERRUPT( 44);
	DEFINE_INTERRUPT( 45);
	DEFINE_INTERRUPT( 46);
	DEFINE_INTERRUPT( 47);
	DEFINE_INTERRUPT( 48);
	DEFINE_INTERRUPT( 49);
	DEFINE_INTERRUPT( 50);
	DEFINE_INTERRUPT( 51);
	DEFINE_INTERRUPT( 52);
	DEFINE_INTERRUPT( 53);
	DEFINE_INTERRUPT( 54);
	DEFINE_INTERRUPT( 55);
	DEFINE_INTERRUPT( 56);
	DEFINE_INTERRUPT( 57);
	DEFINE_INTERRUPT( 58);
	DEFINE_INTERRUPT( 59);
	DEFINE_INTERRUPT( 60);
	DEFINE_INTERRUPT( 61);
	DEFINE_INTERRUPT( 62);
	DEFINE_INTERRUPT( 63);
	DEFINE_INTERRUPT( 64);
	DEFINE_INTERRUPT( 65);
	DEFINE_INTERRUPT( 66);
	DEFINE_INTERRUPT( 67);
	DEFINE_INTERRUPT( 68);
	DEFINE_INTERRUPT( 69);
	DEFINE_INTERRUPT( 70);
	DEFINE_INTERRUPT( 71);
	DEFINE_INTERRUPT( 72);
	DEFINE_INTERRUPT( 73);
	DEFINE_INTERRUPT( 74);
	DEFINE_INTERRUPT( 75);
	DEFINE_INTERRUPT( 76);
	DEFINE_INTERRUPT( 77);
	DEFINE_INTERRUPT( 78);
	DEFINE_INTERRUPT( 79);
	DEFINE_INTERRUPT( 80);
	DEFINE_INTERRUPT( 81);
	DEFINE_INTERRUPT( 82);
	DEFINE_INTERRUPT( 83);
	DEFINE_INTERRUPT( 84);
	DEFINE_INTERRUPT( 85);
	DEFINE_INTERRUPT( 86);
	DEFINE_INTERRUPT( 87);
	DEFINE_INTERRUPT( 88);
	DEFINE_INTERRUPT( 89);
	DEFINE_INTERRUPT( 90);
	DEFINE_INTERRUPT( 91);
	DEFINE_INTERRUPT( 92);
	DEFINE_INTERRUPT( 93);
	DEFINE_INTERRUPT( 94);
	DEFINE_INTERRUPT( 95);
	DEFINE_INTERRUPT( 96);
	DEFINE_INTERRUPT( 97);
	DEFINE_INTERRUPT( 98);
	DEFINE_INTERRUPT( 99);
	DEFINE_INTERRUPT(100);
	DEFINE_INTERRUPT(101);
	DEFINE_INTERRUPT(102);
	DEFINE_INTERRUPT(103);
	DEFINE_INTERRUPT(104);
	DEFINE_INTERRUPT(105);
	DEFINE_INTERRUPT(106);
	DEFINE_INTERRUPT(107);
	DEFINE_INTERRUPT(108);
	DEFINE_INTERRUPT(109);
	DEFINE_INTERRUPT(110);
	DEFINE_INTERRUPT(111);
	DEFINE_INTERRUPT(112);
	DEFINE_INTERRUPT(113);
	DEFINE_INTERRUPT(114);
	DEFINE_INTERRUPT(115);
	DEFINE_INTERRUPT(116);
	DEFINE_INTERRUPT(117);
	DEFINE_INTERRUPT(118);
	DEFINE_INTERRUPT(119);
	DEFINE_INTERRUPT(120);
	DEFINE_INTERRUPT(121);
	DEFINE_INTERRUPT(122);
	DEFINE_INTERRUPT(123);
	DEFINE_INTERRUPT(124);
	DEFINE_INTERRUPT(125);
	DEFINE_INTERRUPT(126);
	DEFINE_INTERRUPT(127);
	DEFINE_INTERRUPT(128);
	DEFINE_INTERRUPT(129);
	DEFINE_INTERRUPT(130);
	DEFINE_INTERRUPT(131);
	DEFINE_INTERRUPT(132);
	DEFINE_INTERRUPT(133);
	DEFINE_INTERRUPT(134);
	DEFINE_INTERRUPT(135);
	DEFINE_INTERRUPT(136);
	DEFINE_INTERRUPT(137);
	DEFINE_INTERRUPT(138);
	DEFINE_INTERRUPT(139);
	DEFINE_INTERRUPT(140);
	DEFINE_INTERRUPT(141);
	DEFINE_INTERRUPT(142);
	DEFINE_INTERRUPT(143);
	DEFINE_INTERRUPT(144);
	DEFINE_INTERRUPT(145);
	DEFINE_INTERRUPT(146);
	DEFINE_INTERRUPT(147);
	DEFINE_INTERRUPT(148);
	DEFINE_INTERRUPT(149);
	DEFINE_INTERRUPT(150);
	DEFINE_INTERRUPT(151);
	DEFINE_INTERRUPT(152);
	DEFINE_INTERRUPT(153);
	DEFINE_INTERRUPT(154);
	DEFINE_INTERRUPT(155);
	DEFINE_INTERRUPT(156);
	DEFINE_INTERRUPT(157);
	DEFINE_INTERRUPT(158);
	DEFINE_INTERRUPT(159);
	DEFINE_INTERRUPT(160);
	DEFINE_INTERRUPT(161);
	DEFINE_INTERRUPT(162);
	DEFINE_INTERRUPT(163);
	DEFINE_INTERRUPT(164);
	DEFINE_INTERRUPT(165);
	DEFINE_INTERRUPT(166);
	DEFINE_INTERRUPT(167);
	DEFINE_INTERRUPT(168);
	DEFINE_INTERRUPT(169);
	DEFINE_INTERRUPT(170);
	DEFINE_INTERRUPT(171);
	DEFINE_INTERRUPT(172);
	DEFINE_INTERRUPT(173);
	DEFINE_INTERRUPT(174);
	DEFINE_INTERRUPT(175);
	DEFINE_INTERRUPT(176);
	DEFINE_INTERRUPT(177);
	DEFINE_INTERRUPT(178);
	DEFINE_INTERRUPT(179);
	DEFINE_INTERRUPT(180);
	DEFINE_INTERRUPT(181);
	DEFINE_INTERRUPT(182);
	DEFINE_INTERRUPT(183);
	DEFINE_INTERRUPT(184);
	DEFINE_INTERRUPT(185);
	DEFINE_INTERRUPT(186);
	DEFINE_INTERRUPT(187);
	DEFINE_INTERRUPT(188);
	DEFINE_INTERRUPT(189);
	DEFINE_INTERRUPT(190);
	DEFINE_INTERRUPT(191);
	DEFINE_INTERRUPT(192);
	DEFINE_INTERRUPT(193);
	DEFINE_INTERRUPT(194);
	DEFINE_INTERRUPT(195);
	DEFINE_INTERRUPT(196);
	DEFINE_INTERRUPT(197);
	DEFINE_INTERRUPT(198);
	DEFINE_INTERRUPT(199);
	DEFINE_INTERRUPT(200);
	DEFINE_INTERRUPT(201);
	DEFINE_INTERRUPT(202);
	DEFINE_INTERRUPT(203);
	DEFINE_INTERRUPT(204);
	DEFINE_INTERRUPT(205);
	DEFINE_INTERRUPT(206);
	DEFINE_INTERRUPT(207);
	DEFINE_INTERRUPT(208);
	DEFINE_INTERRUPT(209);
	DEFINE_INTERRUPT(210);
	DEFINE_INTERRUPT(211);
	DEFINE_INTERRUPT(212);
	DEFINE_INTERRUPT(213);
	DEFINE_INTERRUPT(214);
	DEFINE_INTERRUPT(215);
	DEFINE_INTERRUPT(216);
	DEFINE_INTERRUPT(217);
	DEFINE_INTERRUPT(218);
	DEFINE_INTERRUPT(219);
	DEFINE_INTERRUPT(220);
	DEFINE_INTERRUPT(221);
	DEFINE_INTERRUPT(222);
	DEFINE_INTERRUPT(223);

	// load the idt
	idt_pointer.limit = sizeof(idt) - 1;
	idt_pointer.base = &idt;
	__asm__ volatile(
		"lidt (%0)"
		::
		"r" (&idt_pointer)
		:
		"memory"
	);

	// enable interrupts
	fint_enable();
};

// initializes the GDT, TSS, and IDT for this CPU
//
// the IDT is the same for all CPUs, but the GDT and TSS are per-CPU
void fint_init_secondary_cpu(void) {
	uintptr_t tss_addr = (uintptr_t)&FARCH_PER_CPU(tss);
	void* generic_interrupt_stack_bottom = NULL;
	void* double_fault_stack_bottom = NULL;
	void* debug_stack_bottom = NULL;
	void* page_fault_stack_bottom = NULL;
	farch_int_idt_pointer_t idt_pointer;
	farch_int_gdt_pointer_t gdt_pointer;
	farch_int_idt_entry_t missing_entry;
	uint16_t tss_selector = farch_int_gdt_index_tss * 8;

	// initialize the GDT entries
	FARCH_PER_CPU(gdt).entries[farch_int_gdt_index_null] = 0;
	FARCH_PER_CPU(gdt).entries[farch_int_gdt_index_code] = farch_int_gdt_flags_common | farch_int_gdt_flag_long | farch_int_gdt_flag_executable;
	FARCH_PER_CPU(gdt).entries[farch_int_gdt_index_data] = farch_int_gdt_flags_common;
	FARCH_PER_CPU(gdt).entries[farch_int_gdt_index_tss] = farch_int_gdt_flag_accessed | farch_int_gdt_flag_executable | farch_int_gdt_flag_present | ((sizeof(farch_int_tss_t) - 1ULL) & 0xffffULL) | ((tss_addr & 0xffffffULL) << 16) | (((tss_addr & (0xffULL << 24)) >> 24) << 56);
	FARCH_PER_CPU(gdt).entries[farch_int_gdt_index_tss_other] = (tss_addr & (0xffffffffULL << 32)) >> 32;
	FARCH_PER_CPU(gdt).entries[farch_int_gdt_index_data_user] = farch_int_gdt_flags_common | farch_int_gdt_flag_dpl_ring_3;
	FARCH_PER_CPU(gdt).entries[farch_int_gdt_index_code_user] = farch_int_gdt_flags_common | farch_int_gdt_flag_long | farch_int_gdt_flag_executable | farch_int_gdt_flag_dpl_ring_3;

	// load the gdt
	gdt_pointer.limit = sizeof(FARCH_PER_CPU(gdt)) - 1;
	gdt_pointer.base = &FARCH_PER_CPU(gdt);
	__asm__ volatile(
		"lgdt (%0)"
		::
		"r" (&gdt_pointer)
		:
		"memory"
	);

	// reload the segment registers
	fint_reload_segment_registers(farch_int_gdt_index_code, farch_int_gdt_index_data);

	// load the TSS
	__asm__ volatile(
		"ltr (%0)"
		::
		"r" (&tss_selector)
		:
		"memory"
	);

	// allocate a stack for generic interrupt handlers
	if (fpage_allocate_kernel(IST_STACK_PAGE_COUNT, &generic_interrupt_stack_bottom, fpage_flag_prebound) != ferr_ok) {
		fpanic("failed to allocate stack for generic interrupt handlers");
	}

	// allocate a stack for the double-fault handler
	if (fpage_allocate_kernel(IST_STACK_PAGE_COUNT, &double_fault_stack_bottom, fpage_flag_prebound) != ferr_ok) {
		fpanic("failed to allocate stack for double fault handler");
	}

	// allocate a stack for the debug fault handler
	if (fpage_allocate_kernel(IST_STACK_PAGE_COUNT, &debug_stack_bottom, fpage_flag_prebound) != ferr_ok) {
		fpanic("failed to allocate stack for debug interrupts");
	}

	// allocate a stack for the page fault handler
	if (fpage_allocate_kernel(IST_STACK_PAGE_COUNT, &page_fault_stack_bottom, fpage_flag_prebound) != ferr_ok) {
		fpanic("failed to allocate stack for page fault interrupts");
	}

	// set the stack top addresses
	FARCH_PER_CPU(tss).ist[farch_int_ist_index_generic_interrupt] = (uintptr_t)generic_interrupt_stack_bottom + (FPAGE_PAGE_SIZE * 4);
	FARCH_PER_CPU(tss).ist[farch_int_ist_index_double_fault] = (uintptr_t)double_fault_stack_bottom + (FPAGE_PAGE_SIZE * 4);
	FARCH_PER_CPU(tss).ist[farch_int_ist_index_debug] = (uintptr_t)debug_stack_bottom + (FPAGE_PAGE_SIZE * 4);
	FARCH_PER_CPU(tss).ist[farch_int_ist_index_page_fault] = (uintptr_t)page_fault_stack_bottom + (FPAGE_PAGE_SIZE * 4);;

	// load the idt
	idt_pointer.limit = sizeof(idt) - 1;
	idt_pointer.base = &idt;
	__asm__ volatile(
		"lidt (%0)"
		::
		"r" (&idt_pointer)
		:
		"memory"
	);

	// enable interrupts
	fint_enable();
};

FERRO_NO_RETURN void facpi_reboot_early(void) {
	// the idea here is to corrupt the IDT and the processor should triple-fault
	fint_disable();
	simple_memset(&idt, 0, sizeof(idt));

	// now trigger an interrupt, which should make us triple-fault
	__asm__ volatile("int3" ::: );

	// if we get here, well, crap.
	__builtin_unreachable();
};

ferr_t fint_register_special_handler(uint8_t number, fint_special_handler_f handler, void* data) {
	fint_special_handler_entry_t* entry;
	ferr_t status = ferr_temporary_outage;

	if (number > SPECIAL_HANDLERS_MAX || !handler) {
		return ferr_invalid_argument;
	}

	entry = &special_handlers[number];

	flock_spin_intsafe_lock(&entry->lock);

	if (!entry->handler) {
		status = ferr_ok;
		entry->handler = handler;
		entry->data = data;
	}

	flock_spin_intsafe_unlock(&entry->lock);

	return status;
};
