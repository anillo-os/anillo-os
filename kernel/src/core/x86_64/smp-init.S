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

#include <gen/ferro/offsets.h>
#include <ferro/asm/x86_64/helpers.hS>
#include <ferro/core/x86_64/smp-init.h>
#include <ferro/asm/common.hS>

.text

// align the following code on a page boundary so we can
// copy it into low memory when doing AP initialization
.balign 4096

// the following code should be 16-bit code for real mode
.code16

.global FERRO_SYM(farch_smp_init_code_start)
.global FERRO_SYM(farch_smp_init_code_end)

FERRO_SYM(farch_smp_init_code_start):

init_16:
	// disable (maskable) interrupts
	cli

	// load a zero-length IDT so that interrupts cause a triple-fault during initialization
	lidt (FARCH_SMP_INIT_DATA_BASE + FOFFSET_farch_smp_init_data_idt_pointer)

	// set the PAE and PGE bits in CR4
	mov $0xa0, %eax
	mov %eax, %cr4

	// set up CR3 to point at our (stubbed) root page table
	mov MAKE_IMMEDIATE(FARCH_SMP_INIT_ROOT_TABLE_BASE), %eax
	mov %eax, %cr3

	//
	// set the long-mode-enable bit in the EFER
	//

	// first, read the EFER
	mov $0xc0000080, %ecx
	rdmsr

	// now OR the long-mode-enable bit into the result
	or $0x100, %eax

	// now write the EFER
	wrmsr

	// now active long mode
	// (by OR'ing the protected-mode and paging bits)
	mov %cr0, %eax
	or $0x80000001, %eax
	mov %eax, %cr0

	// load our stubbed GDT
	lgdt (FARCH_SMP_INIT_DATA_BASE + FOFFSET_farch_smp_init_data_gdt_pointer)

	// finally, perform a far jump to enter 64-bit mode
	jmpl $8, $((init_64 - FERRO_SYM(farch_smp_init_code_start)) + FARCH_SMP_INIT_BASE)

// the following code should be for long mode
.code64

// external
.global FERRO_SYM(farch_smp_init_entry)

init_64:
	// load the segment registers with the right data segment
	mov $16, %ax
	mov %ax, %ds
	mov %ax, %es
	mov %ax, %ss

	// load the FS and GS segment registers with the null segment
	mov $0, %ax
	mov %ax, %fs
	mov %ax, %gs

	// set the stack
	mov (FARCH_SMP_INIT_DATA_BASE + FOFFSET_farch_smp_init_data_stack), %rsp

	// offset it to the top of the stack
	addq MAKE_IMMEDIATE(FARCH_SMP_INIT_STACK_SIZE), %rsp

	// push 0 onto the stack (as the fake return address)
	pushq $0

	//
	// jump to the C entry point
	//

	// load the argument
	mov MAKE_IMMEDIATE(FARCH_SMP_INIT_DATA_BASE), %rdi

	// okay so, we can't load the target address directly because although the target address is within RIP-relative range
	// of the *original* address of this code, this code is actually copied into low-memory, so it goes out-of-range.
	// we also can't use 64-bit (or 32-bit) absolute addressing in 64-bit code, so we have to load the target address indirectly.
	//
	// additionally, we have to use RIP-relative addressing to load the target address (and in this case, it's perfectly fine
	// because this constant is copied together with the rest of the code into low-memory).
	mov init_64_jump_addr(%rip), %rax

	jmp *%rax

init_64_jump_addr:
	.quad FERRO_SYM(farch_smp_init_entry)

FERRO_SYM(farch_smp_init_code_end):
