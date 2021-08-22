#include <gen/ferro/offsets.h>

.altmacro

.macro wrapper func, has_code
	.text
	.global farch_int_wrapper_\()\func
	farch_int_wrapper_\()\func\():
		.if \has_code == 0
			subq $136, %rsp
		.else
			subq $128, %rsp
		.endif

		mov %rax, FOFFSET_farch_int_isr_frame_rax(%rsp)
		mov %rcx, FOFFSET_farch_int_isr_frame_rcx(%rsp)
		mov %rdx, FOFFSET_farch_int_isr_frame_rdx(%rsp)
		mov %rbx, FOFFSET_farch_int_isr_frame_rbx(%rsp)
		mov %rsi, FOFFSET_farch_int_isr_frame_rsi(%rsp)
		mov %rdi, FOFFSET_farch_int_isr_frame_rdi(%rsp)
		mov %rbp, FOFFSET_farch_int_isr_frame_rbp(%rsp)
		mov  %r8,  FOFFSET_farch_int_isr_frame_r8(%rsp)
		mov  %r9,  FOFFSET_farch_int_isr_frame_r9(%rsp)
		mov %r10, FOFFSET_farch_int_isr_frame_r10(%rsp)
		mov %r11, FOFFSET_farch_int_isr_frame_r11(%rsp)
		mov %r12, FOFFSET_farch_int_isr_frame_r12(%rsp)
		mov %r13, FOFFSET_farch_int_isr_frame_r13(%rsp)
		mov %r14, FOFFSET_farch_int_isr_frame_r14(%rsp)
		mov %r15, FOFFSET_farch_int_isr_frame_r15(%rsp)
		// leave interrupt-disable alone; higher-level wrappers will take care of it

		.if \has_code == 0
			movq $0, FOFFSET_farch_int_isr_frame_code(%rsp)
		.endif

		mov %rsp, %rdi
		call farch_int_\()\func\()_handler

		mov FOFFSET_farch_int_isr_frame_rax(%rsp), %rax
		mov FOFFSET_farch_int_isr_frame_rcx(%rsp), %rcx
		mov FOFFSET_farch_int_isr_frame_rdx(%rsp), %rdx
		mov FOFFSET_farch_int_isr_frame_rbx(%rsp), %rbx
		mov FOFFSET_farch_int_isr_frame_rsi(%rsp), %rsi
		mov FOFFSET_farch_int_isr_frame_rdi(%rsp), %rdi
		mov FOFFSET_farch_int_isr_frame_rbp(%rsp), %rbp
		mov  FOFFSET_farch_int_isr_frame_r8(%rsp),  %r8
		mov  FOFFSET_farch_int_isr_frame_r9(%rsp),  %r9
		mov FOFFSET_farch_int_isr_frame_r10(%rsp), %r10
		mov FOFFSET_farch_int_isr_frame_r11(%rsp), %r11
		mov FOFFSET_farch_int_isr_frame_r12(%rsp), %r12
		mov FOFFSET_farch_int_isr_frame_r13(%rsp), %r13
		mov FOFFSET_farch_int_isr_frame_r14(%rsp), %r14
		mov FOFFSET_farch_int_isr_frame_r15(%rsp), %r15

		add $136, %rsp

		iretq
.endm

//
// processor-reserved interrupts
//

wrapper debug, 0
wrapper breakpoint, 0
wrapper double_fault, 1
wrapper general_protection, 1
wrapper page_fault, 1

//
// user-defined interrupts
//

.macro misc_wrapper number
	wrapper interrupt_\()\number, 0
.endm

.set i, 0
.rept 224
	misc_wrapper %i
	.set i, i + 1
.endr
