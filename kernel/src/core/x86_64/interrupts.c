/**
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
//
// src/core/x86_64/interrupts.c
//
// x86_64 interrupt handling
//

#include <ferro/core/interrupts.h>
#include <ferro/core/panic.h>
#include <ferro/core/paging.h>
#include <ferro/core/console.h>
#include <ferro/core/locks.h>
#include <libk/libk.h>

#include <stddef.h>

#define FERRO_INTERRUPT __attribute__((interrupt))

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
	fint_gdt_flag_present      = 1ULL << 47,
	fint_gdt_flag_long         = 1ULL << 53,

	fint_gdt_flags_common      = fint_gdt_flag_accessed | fint_gdt_flag_writable | fint_gdt_flag_present | fint_gdt_flag_user_segment,
};

FERRO_PACKED_STRUCT(fint_gdt) {
	uint64_t entries[8];
};

FERRO_ENUM(uint8_t, fint_gdt_index) {
	fint_gdt_index_null,
	fint_gdt_index_code,
	fint_gdt_index_data,
	fint_gdt_index_tss,
	fint_gdt_index_tss_other,
};

FERRO_ENUM(uint8_t, fint_ist_index) {
	fint_ist_index_double_fault,
};

typedef FERRO_INTERRUPT void (*fint_isr_f)(farch_int_isr_frame_t* frame);
typedef FERRO_INTERRUPT void (*fint_isr_with_code_f)(farch_int_isr_frame_t* frame, uint64_t code);
typedef FERRO_INTERRUPT FERRO_NO_RETURN void (*fint_isr_noreturn_f)(farch_int_isr_frame_t* frame);
typedef FERRO_INTERRUPT FERRO_NO_RETURN void (*fint_isr_with_code_noreturn_f)(farch_int_isr_frame_t* frame, uint64_t code);

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
	fint_state_t interrupt_state;
};

FERRO_STRUCT(fint_handler_entry) {
	farch_int_handler_f handler;
	flock_spin_intsafe_t lock;
};

fint_idt_t idt = {0};
fint_handler_entry_t handlers[224] = {0};

fint_tss_t tss = {0};

fint_gdt_t gdt = {
	.entries = {
		// null segment
		0,

		// code segment
		fint_gdt_flags_common | fint_gdt_flag_long | fint_gdt_flag_executable,

		// data segment
		fint_gdt_flags_common,

		// TSS segment
		// occupies two entries
		// needs to be initialized with the pointer value in `fint_init`
		fint_gdt_flag_accessed | fint_gdt_flag_executable | fint_gdt_flag_present | ((sizeof(fint_tss_t) - 1ULL) & 0xffffULL),
		0,
	},
};

static void fint_handler_common_begin(fint_handler_common_data_t* data) {
	// for all our handlers, we set a bit in their configuration to tell the CPU to disable interrupts when handling them
	// so we need to let our interrupt management code know this
	data->interrupt_state = FARCH_PER_CPU(outstanding_interrupt_disable_count);
	FARCH_PER_CPU(outstanding_interrupt_disable_count) = 1;
};

static void fint_handler_common_end(fint_handler_common_data_t* data) {
	FARCH_PER_CPU(outstanding_interrupt_disable_count) = data->interrupt_state;
};

static FERRO_INTERRUPT void breakpoint_handler(farch_int_isr_frame_t* frame) {
	fint_handler_common_data_t data;

	fint_handler_common_begin(&data);

	fconsole_logf("breakpoint at %p\n", (void*)((uintptr_t)frame->instruction_pointer - 1));

	fint_handler_common_end(&data);
};

static FERRO_INTERRUPT FERRO_NO_RETURN void double_fault_handler(farch_int_isr_frame_t* frame, uint64_t code) {
	fint_handler_common_data_t data;

	fint_handler_common_begin(&data);

	fconsole_log("double faulted; going down now...\n");
	fpanic("double fault");

	// unnecessary, but just for consistency
	fint_handler_common_end(&data);
};

static FERRO_INTERRUPT void general_protection_fault_handler(farch_int_isr_frame_t* frame, uint64_t code) {
	fconsole_logf("received general protection fault with code %lu at %p\n", code, frame->instruction_pointer);
};

#define INTERRUPT_HANDLER(number) \
	static FERRO_INTERRUPT void interrupt_ ## number ## _handler(farch_int_isr_frame_t* frame) { \
		fint_handler_common_data_t data; \
		farch_int_handler_f handler = NULL; \
		fint_handler_common_begin(&data); \
		flock_spin_intsafe_lock(&handlers[number].lock); \
		handler = handlers[number].handler; \
		flock_spin_intsafe_unlock(&handlers[number].lock); \
		if (handler) { \
			handler(frame); \
		} else { \
			fpanic("Unhandled interrupt " #number); \
		} \
		fint_handler_common_end(&data); \
	};

INTERRUPT_HANDLER(  0);
INTERRUPT_HANDLER(  1);
INTERRUPT_HANDLER(  2);
INTERRUPT_HANDLER(  3);
INTERRUPT_HANDLER(  4);
INTERRUPT_HANDLER(  5);
INTERRUPT_HANDLER(  6);
INTERRUPT_HANDLER(  7);
INTERRUPT_HANDLER(  8);
INTERRUPT_HANDLER(  9);
INTERRUPT_HANDLER( 10);
INTERRUPT_HANDLER( 11);
INTERRUPT_HANDLER( 12);
INTERRUPT_HANDLER( 13);
INTERRUPT_HANDLER( 14);
INTERRUPT_HANDLER( 15);
INTERRUPT_HANDLER( 16);
INTERRUPT_HANDLER( 17);
INTERRUPT_HANDLER( 18);
INTERRUPT_HANDLER( 19);
INTERRUPT_HANDLER( 20);
INTERRUPT_HANDLER( 21);
INTERRUPT_HANDLER( 22);
INTERRUPT_HANDLER( 23);
INTERRUPT_HANDLER( 24);
INTERRUPT_HANDLER( 25);
INTERRUPT_HANDLER( 26);
INTERRUPT_HANDLER( 27);
INTERRUPT_HANDLER( 28);
INTERRUPT_HANDLER( 29);
INTERRUPT_HANDLER( 30);
INTERRUPT_HANDLER( 31);
INTERRUPT_HANDLER( 32);
INTERRUPT_HANDLER( 33);
INTERRUPT_HANDLER( 34);
INTERRUPT_HANDLER( 35);
INTERRUPT_HANDLER( 36);
INTERRUPT_HANDLER( 37);
INTERRUPT_HANDLER( 38);
INTERRUPT_HANDLER( 39);
INTERRUPT_HANDLER( 40);
INTERRUPT_HANDLER( 41);
INTERRUPT_HANDLER( 42);
INTERRUPT_HANDLER( 43);
INTERRUPT_HANDLER( 44);
INTERRUPT_HANDLER( 45);
INTERRUPT_HANDLER( 46);
INTERRUPT_HANDLER( 47);
INTERRUPT_HANDLER( 48);
INTERRUPT_HANDLER( 49);
INTERRUPT_HANDLER( 50);
INTERRUPT_HANDLER( 51);
INTERRUPT_HANDLER( 52);
INTERRUPT_HANDLER( 53);
INTERRUPT_HANDLER( 54);
INTERRUPT_HANDLER( 55);
INTERRUPT_HANDLER( 56);
INTERRUPT_HANDLER( 57);
INTERRUPT_HANDLER( 58);
INTERRUPT_HANDLER( 59);
INTERRUPT_HANDLER( 60);
INTERRUPT_HANDLER( 61);
INTERRUPT_HANDLER( 62);
INTERRUPT_HANDLER( 63);
INTERRUPT_HANDLER( 64);
INTERRUPT_HANDLER( 65);
INTERRUPT_HANDLER( 66);
INTERRUPT_HANDLER( 67);
INTERRUPT_HANDLER( 68);
INTERRUPT_HANDLER( 69);
INTERRUPT_HANDLER( 70);
INTERRUPT_HANDLER( 71);
INTERRUPT_HANDLER( 72);
INTERRUPT_HANDLER( 73);
INTERRUPT_HANDLER( 74);
INTERRUPT_HANDLER( 75);
INTERRUPT_HANDLER( 76);
INTERRUPT_HANDLER( 77);
INTERRUPT_HANDLER( 78);
INTERRUPT_HANDLER( 79);
INTERRUPT_HANDLER( 80);
INTERRUPT_HANDLER( 81);
INTERRUPT_HANDLER( 82);
INTERRUPT_HANDLER( 83);
INTERRUPT_HANDLER( 84);
INTERRUPT_HANDLER( 85);
INTERRUPT_HANDLER( 86);
INTERRUPT_HANDLER( 87);
INTERRUPT_HANDLER( 88);
INTERRUPT_HANDLER( 89);
INTERRUPT_HANDLER( 90);
INTERRUPT_HANDLER( 91);
INTERRUPT_HANDLER( 92);
INTERRUPT_HANDLER( 93);
INTERRUPT_HANDLER( 94);
INTERRUPT_HANDLER( 95);
INTERRUPT_HANDLER( 96);
INTERRUPT_HANDLER( 97);
INTERRUPT_HANDLER( 98);
INTERRUPT_HANDLER( 99);
INTERRUPT_HANDLER(100);
INTERRUPT_HANDLER(101);
INTERRUPT_HANDLER(102);
INTERRUPT_HANDLER(103);
INTERRUPT_HANDLER(104);
INTERRUPT_HANDLER(105);
INTERRUPT_HANDLER(106);
INTERRUPT_HANDLER(107);
INTERRUPT_HANDLER(108);
INTERRUPT_HANDLER(109);
INTERRUPT_HANDLER(110);
INTERRUPT_HANDLER(111);
INTERRUPT_HANDLER(112);
INTERRUPT_HANDLER(113);
INTERRUPT_HANDLER(114);
INTERRUPT_HANDLER(115);
INTERRUPT_HANDLER(116);
INTERRUPT_HANDLER(117);
INTERRUPT_HANDLER(118);
INTERRUPT_HANDLER(119);
INTERRUPT_HANDLER(120);
INTERRUPT_HANDLER(121);
INTERRUPT_HANDLER(122);
INTERRUPT_HANDLER(123);
INTERRUPT_HANDLER(124);
INTERRUPT_HANDLER(125);
INTERRUPT_HANDLER(126);
INTERRUPT_HANDLER(127);
INTERRUPT_HANDLER(128);
INTERRUPT_HANDLER(129);
INTERRUPT_HANDLER(130);
INTERRUPT_HANDLER(131);
INTERRUPT_HANDLER(132);
INTERRUPT_HANDLER(133);
INTERRUPT_HANDLER(134);
INTERRUPT_HANDLER(135);
INTERRUPT_HANDLER(136);
INTERRUPT_HANDLER(137);
INTERRUPT_HANDLER(138);
INTERRUPT_HANDLER(139);
INTERRUPT_HANDLER(140);
INTERRUPT_HANDLER(141);
INTERRUPT_HANDLER(142);
INTERRUPT_HANDLER(143);
INTERRUPT_HANDLER(144);
INTERRUPT_HANDLER(145);
INTERRUPT_HANDLER(146);
INTERRUPT_HANDLER(147);
INTERRUPT_HANDLER(148);
INTERRUPT_HANDLER(149);
INTERRUPT_HANDLER(150);
INTERRUPT_HANDLER(151);
INTERRUPT_HANDLER(152);
INTERRUPT_HANDLER(153);
INTERRUPT_HANDLER(154);
INTERRUPT_HANDLER(155);
INTERRUPT_HANDLER(156);
INTERRUPT_HANDLER(157);
INTERRUPT_HANDLER(158);
INTERRUPT_HANDLER(159);
INTERRUPT_HANDLER(160);
INTERRUPT_HANDLER(161);
INTERRUPT_HANDLER(162);
INTERRUPT_HANDLER(163);
INTERRUPT_HANDLER(164);
INTERRUPT_HANDLER(165);
INTERRUPT_HANDLER(166);
INTERRUPT_HANDLER(167);
INTERRUPT_HANDLER(168);
INTERRUPT_HANDLER(169);
INTERRUPT_HANDLER(170);
INTERRUPT_HANDLER(171);
INTERRUPT_HANDLER(172);
INTERRUPT_HANDLER(173);
INTERRUPT_HANDLER(174);
INTERRUPT_HANDLER(175);
INTERRUPT_HANDLER(176);
INTERRUPT_HANDLER(177);
INTERRUPT_HANDLER(178);
INTERRUPT_HANDLER(179);
INTERRUPT_HANDLER(180);
INTERRUPT_HANDLER(181);
INTERRUPT_HANDLER(182);
INTERRUPT_HANDLER(183);
INTERRUPT_HANDLER(184);
INTERRUPT_HANDLER(185);
INTERRUPT_HANDLER(186);
INTERRUPT_HANDLER(187);
INTERRUPT_HANDLER(188);
INTERRUPT_HANDLER(189);
INTERRUPT_HANDLER(190);
INTERRUPT_HANDLER(191);
INTERRUPT_HANDLER(192);
INTERRUPT_HANDLER(193);
INTERRUPT_HANDLER(194);
INTERRUPT_HANDLER(195);
INTERRUPT_HANDLER(196);
INTERRUPT_HANDLER(197);
INTERRUPT_HANDLER(198);
INTERRUPT_HANDLER(199);
INTERRUPT_HANDLER(200);
INTERRUPT_HANDLER(201);
INTERRUPT_HANDLER(202);
INTERRUPT_HANDLER(203);
INTERRUPT_HANDLER(204);
INTERRUPT_HANDLER(205);
INTERRUPT_HANDLER(206);
INTERRUPT_HANDLER(207);
INTERRUPT_HANDLER(208);
INTERRUPT_HANDLER(209);
INTERRUPT_HANDLER(210);
INTERRUPT_HANDLER(211);
INTERRUPT_HANDLER(212);
INTERRUPT_HANDLER(213);
INTERRUPT_HANDLER(214);
INTERRUPT_HANDLER(215);
INTERRUPT_HANDLER(216);
INTERRUPT_HANDLER(217);
INTERRUPT_HANDLER(218);
INTERRUPT_HANDLER(219);
INTERRUPT_HANDLER(220);
INTERRUPT_HANDLER(221);
INTERRUPT_HANDLER(222);
INTERRUPT_HANDLER(223);

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

void fint_init(void) {
	uintptr_t tss_addr = (uintptr_t)&tss;
	void* stack_bottom = NULL;
	fint_idt_pointer_t idt_pointer;
	fint_gdt_pointer_t gdt_pointer;
	fint_idt_entry_t missing_entry;
	uint16_t tss_selector = fint_gdt_index_tss * 8;

	// initialize the TSS address in the GDT
	gdt.entries[fint_gdt_index_tss] |= ((tss_addr & 0xffffffULL) << 16) | (((tss_addr & (0xffULL << 24)) >> 24) << 56);
	gdt.entries[fint_gdt_index_tss_other] = (tss_addr & (0xffffffffULL << 32)) >> 32;

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
	fint_reload_segment_registers(fint_gdt_index_code, fint_gdt_index_data);

	// load the TSS
	__asm__ volatile(
		"ltr (%0)"
		::
		"r" (&tss_selector)
		:
		"memory"
	);

	// allocate a stack for the double-fault handler
	if (fpage_allocate_kernel(4, &stack_bottom) != ferr_ok) {
		fpanic("failed to allocate stack for double fault handler");
	}

	// set the stack top address
	tss.ist[fint_ist_index_double_fault] = (uintptr_t)stack_bottom + (FPAGE_PAGE_SIZE * 4);

	// initialize the idt with missing entries (they still require certain bits to be 1)
	fint_make_idt_entry(&missing_entry, NULL, 0, 0, false, 0);
	missing_entry.options &= ~fint_idt_entry_option_present;
	memclone(&idt, &missing_entry, sizeof(missing_entry), sizeof(idt) / sizeof(missing_entry));

	// initialize the desired idt entries with actual values
	fint_make_idt_entry(&idt.breakpoint, breakpoint_handler, fint_gdt_index_code, 0, false, 0);
	fint_make_idt_entry(&idt.double_fault, double_fault_handler, fint_gdt_index_code, 1, false, 0);
	fint_make_idt_entry(&idt.general_protection_fault, general_protection_fault_handler, fint_gdt_index_code, 0, false, 0);

	// initialize the array of miscellaneous interrupts
	#define DEFINE_INTERRUPT(number) \
		flock_spin_intsafe_init(&handlers[number].lock); \
		fint_make_idt_entry(&idt.interrupts[number], interrupt_ ## number ## _handler, fint_gdt_index_code, 0, false, 0);

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

	// test: trigger a breakpoint
	//__builtin_debugtrap();

	// test: trigger a double fault
	//*(volatile char*)0xdeadbeefULL = 42;
};
