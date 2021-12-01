//
// layout of rsp on entry:
//   [0..8]   = image handle address (typically the address of `_dyld_private` in the image)
//   [8..16]  = lazy binding info offset (from the compressed dynamic linker information structure)
//   [16..24] = return address
//
.text
.globl _dymple_bind_stub     // defined in C
.globl _dymple_bind_stub_raw // defined here
_dymple_bind_stub_raw:
	// save rbp (for debugging purposes)
	pushq %rbp
	mov %rsp, %rbp

	// make room on the stack for the caller's parameters
	subq $0x40, %rsp

	// now save the parameters
	movq %rdi, 0x00(%rsp)
	movq %rsi, 0x08(%rsp)
	movq %rdx, 0x10(%rsp)
	movq %rcx, 0x18(%rsp)
	movq  %r8, 0x20(%rsp)
	movq  %r9, 0x28(%rsp)
	movq %rbx, 0x30(%rsp)
	movq %rax, 0x38(%rsp)

	//
	// now the layout of rsp looks like:
	//   [0..8]   = rdi
	//   [8..16]  = rsi
	//   [16..24] = rdx
	//   [24..32] = rcx
	//   [32..40] = r8
	//   [40..48] = r9
	//   [48..56] = rbx
	//   [56..64] = rax
	//   [64..72] = saved rbp
	//   [72..80] = image handle address
	//   [80..88] = lazy binding info offset
	//   [88..96] = return address
	//
	// rbx and rax are sort of weird to save since they're not really parameters,
	// but Apple's dyld saves them, so we do, too.
	//

	// now set arg 1 (rdi) to a pointer to the info structure
	lea 0x48(%rsp), %rdi

	// and call our actual binder
	call _dymple_bind_stub

	// our binder returns the address where we should jump.
	// load it into r11 (which is guaranteed not to be used for parameters).
	movq %rax, %r11

	// let's restore our saved parameters
	movq 0x00(%rsp), %rdi
	movq 0x08(%rsp), %rsi
	movq 0x10(%rsp), %rdx
	movq 0x18(%rsp), %rcx
	movq 0x20(%rsp),  %r8
	movq 0x28(%rsp),  %r9
	movq 0x30(%rsp), %rbx
	movq 0x38(%rsp), %rax

	// restore the stack back to the old rbp
	movq %rbp, %rsp

	// restore the old rbp
	popq %rbp

	// and remove our lazy binding info
	addq $0x10, %rsp

	// now the only thing on the stack should be the return address,
	// which is for the function we're about to jump to
	jmp *%r11

.global ___stack_chk_fail
___stack_chk_fail:
	ud2

.data
.global ___stack_chk_guard
___stack_chk_guard:
	.long 0
