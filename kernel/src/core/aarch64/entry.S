.section .text.ferro_entry

.global _ferro_raw_entry
_ferro_raw_entry:
	ldr x0, =stack_top // load the stack address
	mov sp, x0         // load the stack address into the stack pointer
	bl ferro_entry     // jump into the C code
	b .                // should never be reached, but just in case
