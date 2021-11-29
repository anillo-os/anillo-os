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

#include <stddef.h>

#define IST_STACK_PAGE_COUNT 4

FERRO_PACKED_STRUCT(fint_tss) {
	uint32_t reserved1;
	uint64_t pst[3];
	uint64_t reserved2;
	uint64_t ist[7];
	uint64_t reserved3;
	uint16_t reserved4;
	uint16_t iomap_offset;
};

FERRO_OPTIONS(uint64_t, fint_gdt_flags) {
	fint_gdt_flag_accessed     = 1ULL << 40,
	fint_gdt_flag_writable     = 1ULL << 41,
	fint_gdt_flag_executable   = 1ULL << 43,
	fint_gdt_flag_user_segment = 1ULL << 44,
	fint_gdt_flag_dpl_ring_3   = 3ULL << 45,
	fint_gdt_flag_present      = 1ULL << 47,
	fint_gdt_flag_long         = 1ULL << 53,

	fint_gdt_flags_common      = fint_gdt_flag_accessed | fint_gdt_flag_writable | fint_gdt_flag_present | fint_gdt_flag_user_segment,

	// just to shut clang up
	fint_gdt_flag_dpl_ring_3_hi = 1ULL << 46,
	fint_gdt_flag_dpl_ring_3_lo = 1ULL << 45,
};

FERRO_PACKED_STRUCT(fint_gdt) {
	uint64_t entries[8];
};

FERRO_ENUM(uint8_t, fint_ist_index) {
	// used for all interrupts without their own IST stack
	fint_ist_index_generic_interrupt,

	// used for the double fault handler
	fint_ist_index_double_fault,
};

typedef void (*fint_isr_f)(fint_frame_t* frame);
typedef void (*fint_isr_with_code_f)(fint_frame_t* frame);
typedef FERRO_NO_RETURN void (*fint_isr_noreturn_f)(fint_frame_t* frame);
typedef FERRO_NO_RETURN void (*fint_isr_with_code_noreturn_f)(fint_frame_t* frame);

FERRO_OPTIONS(uint16_t, fint_idt_entry_options) {
	fint_idt_entry_option_enable_interrupts = 1 << 8,
	fint_idt_entry_option_present           = 1 << 15,
};

FERRO_PACKED_STRUCT(fint_idt_entry) {
	uint16_t pointer_low_16;
	uint16_t code_segment_index;
	uint16_t options;
	uint16_t pointer_mid_16;
	uint32_t pointer_high_32;
	uint32_t reserved;
};

FERRO_ALWAYS_INLINE void fint_make_idt_entry(fint_idt_entry_t* out_entry, void* isr, uint8_t code_segment_index, uint8_t ist_index, bool enable_interrupts, uint8_t privilege_level) {
	uintptr_t isr_addr = (uintptr_t)isr;

	out_entry->pointer_low_16 = isr_addr & 0xffffULL;
	out_entry->pointer_mid_16 = (isr_addr & (0xffffULL << 16)) >> 16;
	out_entry->pointer_high_32 = (isr_addr & (0xffffffffULL << 32)) >> 32;

	out_entry->code_segment_index = code_segment_index * 8;

	out_entry->options = 0xe00 | (enable_interrupts ? fint_idt_entry_option_enable_interrupts : 0) | fint_idt_entry_option_present | ((privilege_level & 3) << 13) | (ist_index & 7);

	out_entry->reserved = 0;
};

/**
 * Here are the function types of each of the following interrupt entries:
 * ```c
 * fint_isr_t division_error;
 * fint_isr_t debug;
 * fint_isr_t nmi;
 * fint_isr_t breakpoint;
 * fint_isr_t overflow;
 * fint_isr_t bounds_check_failure;
 * fint_isr_t invalid_opcode;
 * fint_isr_t device_not_available;
 * fint_isr_with_code_noreturn_t double_fault;
 * fint_isr_t reserved_9;
 * fint_isr_with_code_t invalid_tss;
 * fint_isr_with_code_t segment_not_present;
 * fint_isr_with_code_t stack_segment_fault;
 * fint_isr_with_code_t general_protection_fault;
 * fint_isr_with_code_t page_fault;
 * fint_isr_t reserved_15;
 * fint_isr_t x87_exception;
 * fint_isr_with_code_t alignment_check_failure;
 * fint_isr_noreturn_t machine_check;
 * fint_isr_t simd_exception;
 * fint_isr_t virtualization_exception;
 * fint_isr_t reserved_21;
 * fint_isr_t reserved_22;
 * fint_isr_t reserved_23;
 * fint_isr_t reserved_24;
 * fint_isr_t reserved_25;
 * fint_isr_t reserved_26;
 * fint_isr_t reserved_27;
 * fint_isr_t reserved_28;
 * fint_isr_t reserved_29;
 * fint_isr_with_code_t security_exception;
 * fint_isr_t reserved_31;
 * 
 * fint_isr_t interrupts[224];
 * ```
 */
FERRO_PACKED_STRUCT(fint_idt) {
	fint_idt_entry_t division_error;
	fint_idt_entry_t debug;
	fint_idt_entry_t nmi;
	fint_idt_entry_t breakpoint;
	fint_idt_entry_t overflow;
	fint_idt_entry_t bounds_check_failure;
	fint_idt_entry_t invalid_opcode;
	fint_idt_entry_t device_not_available;
	fint_idt_entry_t double_fault;
	fint_idt_entry_t reserved_9;
	fint_idt_entry_t invalid_tss;
	fint_idt_entry_t segment_not_present;
	fint_idt_entry_t stack_segment_fault;
	fint_idt_entry_t general_protection_fault;
	fint_idt_entry_t page_fault;
	fint_idt_entry_t reserved_15;
	fint_idt_entry_t x87_exception;
	fint_idt_entry_t alignment_check_failure;
	fint_idt_entry_t machine_check;
	fint_idt_entry_t simd_exception;
	fint_idt_entry_t virtualization_exception;
	fint_idt_entry_t reserved_21;
	fint_idt_entry_t reserved_22;
	fint_idt_entry_t reserved_23;
	fint_idt_entry_t reserved_24;
	fint_idt_entry_t reserved_25;
	fint_idt_entry_t reserved_26;
	fint_idt_entry_t reserved_27;
	fint_idt_entry_t reserved_28;
	fint_idt_entry_t reserved_29;
	fint_idt_entry_t security_exception;
	fint_idt_entry_t reserved_31;

	fint_idt_entry_t interrupts[224];
};

FERRO_PACKED_STRUCT(fint_idt_pointer) {
	uint16_t limit;
	fint_idt_t* base;
};

FERRO_PACKED_STRUCT(fint_gdt_pointer) {
	uint16_t limit;
	fint_gdt_t* base;
};

FERRO_STRUCT(fint_handler_common_data) {
	fint_frame_t* previous_exception_frame;
};

FERRO_STRUCT(fint_handler_entry) {
	farch_int_handler_f handler;
	flock_spin_intsafe_t lock;
};

FERRO_STRUCT(fint_special_handler_entry) {
	fint_special_handler_f handler;
	void* data;
	flock_spin_intsafe_t lock;
};

static fint_idt_t idt = {0};
static fint_handler_entry_t handlers[224] = {0};

#define SPECIAL_HANDLERS_MAX fint_special_interrupt_common_LAST
static fint_special_handler_entry_t special_handlers[SPECIAL_HANDLERS_MAX] = {0};

static fint_tss_t tss = {0};

static fint_gdt_t gdt = {
	.entries = {
		// null segment
		0,

		// code segment
		fint_gdt_flags_common | fint_gdt_flag_long | fint_gdt_flag_executable,

		// data segment
		fint_gdt_flags_common,

		// TSS segment
		// occupies two entries
		// needs to be initialized with the pointer value in fint_init()
		fint_gdt_flag_accessed | fint_gdt_flag_executable | fint_gdt_flag_present | ((sizeof(fint_tss_t) - 1ULL) & 0xffffULL),
		0,

		// user data segment
		fint_gdt_flags_common | fint_gdt_flag_dpl_ring_3,

		// user code segment
		fint_gdt_flags_common | fint_gdt_flag_long | fint_gdt_flag_executable | fint_gdt_flag_dpl_ring_3,
	},
};

static void fint_handler_common_begin(fint_handler_common_data_t* data, fint_frame_t* frame, bool safe_mode) {
	// for all our handlers, we set a bit in their configuration to tell the CPU to disable interrupts when handling them
	// so we need to let our interrupt management code know this
	frame->saved_registers.interrupt_disable = FARCH_PER_CPU(outstanding_interrupt_disable_count);
	FARCH_PER_CPU(outstanding_interrupt_disable_count) = 1;

	// we also need to set the current interrupt frame
	data->previous_exception_frame = FARCH_PER_CPU(current_exception_frame);
	FARCH_PER_CPU(current_exception_frame) = frame;

	if (!safe_mode && FARCH_PER_CPU(current_thread)) {
		fthread_interrupt_start(FARCH_PER_CPU(current_thread));
	}
};

static void fint_handler_common_end(fint_handler_common_data_t* data, fint_frame_t* frame, bool safe_mode) {
	if (!safe_mode && FARCH_PER_CPU(current_thread)) {
		fthread_interrupt_end(FARCH_PER_CPU(current_thread));
	}

	FARCH_PER_CPU(current_exception_frame) = data->previous_exception_frame;
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

	fint_handler_common_begin(&data, frame, true);

	fconsole_logf("general protection fault; code=%llu; frame:\n", frame->code);
	print_frame(frame);
	trace_stack((void*)frame->saved_registers.rbp);
	fpanic("general protection fault");

	fint_handler_common_end(&data, frame, true);
};

INTERRUPT_HANDLER(page_fault) {
	fint_handler_common_data_t data;
	uintptr_t faulting_address = 0;

	fint_handler_common_begin(&data, frame, true);

	__asm__ volatile("mov %%cr2, %0" : "=r" (faulting_address));

	fconsole_logf("page fault; code=%llu; faulting address=%p; frame:\n", frame->code, (void*)faulting_address);
	fconsole_log("page fault code description: ");
	print_page_fault_code(frame->code);
	fconsole_log("\n");
	print_frame(frame);
	trace_stack((void*)frame->saved_registers.rbp);
	fpanic("page fault");

	fint_handler_common_end(&data, frame, true);
};

INTERRUPT_HANDLER(invalid_opcode) {
	fint_handler_common_data_t data;

	fint_handler_common_begin(&data, frame, true);

	fconsole_logf("invalid opcode; frame:\n");
	print_frame(frame);
	trace_stack((void*)frame->saved_registers.rbp);
	fpanic("invalid opcode");

	fint_handler_common_end(&data, frame, true);
};

#define MISC_INTERRUPT_HANDLER(number) \
	INTERRUPT_HANDLER(interrupt_ ## number) { \
		fint_handler_common_data_t data; \
		farch_int_handler_f handler = NULL; \
		fint_handler_common_begin(&data, frame, false); \
		flock_spin_intsafe_lock(&handlers[number].lock); \
		handler = handlers[number].handler; \
		flock_spin_intsafe_unlock(&handlers[number].lock); \
		if (handler) { \
			handler(frame); \
		} else { \
			fpanic("Unhandled interrupt " #number); \
		} \
		fint_handler_common_end(&data, frame, false); \
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

ferr_t farch_int_register_handler(uint8_t interrupt, farch_int_handler_f handler) {
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

out:
	flock_spin_intsafe_unlock(&entry->lock);
out_unlocked:
	return ferr_ok;
};

ferr_t farch_int_unregister_handler(uint8_t interrupt) {
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

out:
	flock_spin_intsafe_unlock(&entry->lock);
out_unlocked:
	return ferr_ok;
};

uint8_t farch_int_next_available(void) {
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

void fint_init(void) {
	uintptr_t tss_addr = (uintptr_t)&tss;
	void* generic_interrupt_stack_bottom = NULL;
	void* double_fault_stack_bottom = NULL;
	fint_idt_pointer_t idt_pointer;
	fint_gdt_pointer_t gdt_pointer;
	fint_idt_entry_t missing_entry;
	uint16_t tss_selector = farch_int_gdt_index_tss * 8;

	// initialize the TSS address in the GDT
	gdt.entries[farch_int_gdt_index_tss] |= ((tss_addr & 0xffffffULL) << 16) | (((tss_addr & (0xffULL << 24)) >> 24) << 56);
	gdt.entries[farch_int_gdt_index_tss_other] = (tss_addr & (0xffffffffULL << 32)) >> 32;

	// load the gdt
	gdt_pointer.limit = sizeof(gdt) - 1;
	gdt_pointer.base = &gdt;
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
	if (fpage_allocate_kernel(IST_STACK_PAGE_COUNT, &generic_interrupt_stack_bottom, 0) != ferr_ok) {
		fpanic("failed to allocate stack for generic interrupt handlers");
	}

	// allocate a stack for the double-fault handler
	if (fpage_allocate_kernel(IST_STACK_PAGE_COUNT, &double_fault_stack_bottom, 0) != ferr_ok) {
		fpanic("failed to allocate stack for double fault handler");
	}

	// set the stack top addresses
	tss.ist[fint_ist_index_generic_interrupt] = (uintptr_t)generic_interrupt_stack_bottom + (FPAGE_PAGE_SIZE * 4);
	tss.ist[fint_ist_index_double_fault] = (uintptr_t)double_fault_stack_bottom + (FPAGE_PAGE_SIZE * 4);

	// initialize the idt with missing entries (they still require certain bits to be 1)
	fint_make_idt_entry(&missing_entry, NULL, 0, 0, false, 0);
	missing_entry.options &= ~fint_idt_entry_option_present;
	simple_memclone(&idt, &missing_entry, sizeof(missing_entry), sizeof(idt) / sizeof(missing_entry));

	// initialize the desired idt entries with actual values
	fint_make_idt_entry(&idt.debug, farch_int_wrapper_debug, farch_int_gdt_index_code, fint_ist_index_generic_interrupt + 1, false, 0);
	fint_make_idt_entry(&idt.breakpoint, farch_int_wrapper_breakpoint, farch_int_gdt_index_code, fint_ist_index_generic_interrupt + 1, false, 0);
	fint_make_idt_entry(&idt.double_fault, farch_int_wrapper_double_fault, farch_int_gdt_index_code, fint_ist_index_double_fault + 1, false, 0);
	fint_make_idt_entry(&idt.general_protection_fault, farch_int_wrapper_general_protection, farch_int_gdt_index_code, fint_ist_index_generic_interrupt + 1, false, 0);
	fint_make_idt_entry(&idt.page_fault, farch_int_wrapper_page_fault, farch_int_gdt_index_code, fint_ist_index_generic_interrupt + 1, false, 0);
	fint_make_idt_entry(&idt.invalid_opcode, farch_int_wrapper_invalid_opcode, farch_int_gdt_index_code, fint_ist_index_generic_interrupt + 1, false, 0);

	// initialize the array of miscellaneous interrupts
	#define DEFINE_INTERRUPT(number) \
		flock_spin_intsafe_init(&handlers[number].lock); \
		fint_make_idt_entry(&idt.interrupts[number], farch_int_wrapper_interrupt_ ## number, farch_int_gdt_index_code, fint_ist_index_generic_interrupt + 1, false, 0);

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

	// initialize the array of special interrupts
	for (size_t i = 0; i < sizeof(special_handlers) / sizeof(*special_handlers); ++i) {
		flock_spin_intsafe_init(&special_handlers[i].lock);
	}

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
