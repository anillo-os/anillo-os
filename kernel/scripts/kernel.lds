ENTRY(kernel_start_physical)

kernel_base_virtual  = 0xffff800000000000;
kernel_base_physical = 0x0000000000000000;

kernel_start_physical = kernel_start_virtual - kernel_base_virtual;

SECTIONS {
	. = kernel_base_virtual + kernel_base_physical;

	kernel_start_virtual = .;

	.text : AT(ADDR(.text) - kernel_base_virtual) {
		KEEP(*(.text.ferro_entry))
		*(.text.*)
	}

	. = ALIGN(1024);

	.data : AT(ADDR(.data) - kernel_base_virtual) {
		*(.data.*)
	}

	. = ALIGN(1024);

	.rodata : AT(ADDR(.rodata) - kernel_base_virtual) {
		*(.rodata.*)
	}

	. = ALIGN(1024);

	.bss : AT(ADDR(.bss) - kernel_base_virtual) {
		kernel_bss_start = .;

		*(.bss)
		*(.bss.*)
		*(COMMON)

		kernel_bss_end = .;
	}

	kernel_end_virtual = .;

	/DISCARD/ : {
		*(.note.*)
		*(.comment.*)
	}
}
