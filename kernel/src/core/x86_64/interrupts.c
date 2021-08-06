#include <ferro/core/interrupts.h>
#include <ferro/core/panic.h>
#include <ferro/core/paging.h>
#include <ferro/core/console.h>
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

FERRO_PACKED_STRUCT(fint_isr_frame) {
	void* instruction_pointer;
	uint64_t code_segment;
	uint64_t cpu_flags;
	void* stack_pointer;
	uint64_t stack_segment;
};


typedef FERRO_INTERRUPT void (*fint_isr_t)(fint_isr_frame_t* frame);
typedef FERRO_INTERRUPT void (*fint_isr_with_code_t)(fint_isr_frame_t* frame, uint64_t code);
typedef FERRO_INTERRUPT FERRO_NO_RETURN void (*fint_isr_noreturn_t)(fint_isr_frame_t* frame);
typedef FERRO_INTERRUPT FERRO_NO_RETURN void (*fint_isr_with_code_noreturn_t)(fint_isr_frame_t* frame, uint64_t code);

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

fint_idt_t idt = {0};

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

FERRO_INTERRUPT void breakpoint_handler(fint_isr_frame_t* frame) {
	fint_handler_common_data_t data;

	fint_handler_common_begin(&data);

	fconsole_logf("breakpoint at %p\n", (void*)((uintptr_t)frame->instruction_pointer - 1));

	fint_handler_common_end(&data);
};

FERRO_INTERRUPT FERRO_NO_RETURN void double_fault_handler(fint_isr_frame_t* frame, uint64_t code) {
	fint_handler_common_data_t data;

	fint_handler_common_begin(&data);

	fconsole_log("double faulted; going down now...\n");
	fpanic();

	// unnecessary, but just for consistency
	fint_handler_common_end(&data);
};

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
		fpanic();
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
