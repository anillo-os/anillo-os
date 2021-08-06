#include <ferro/core/interrupts.h>
#include <ferro/core/panic.h>
#include <ferro/core/console.h>

FERRO_PACKED_STRUCT(fint_vector_table_block) {
	uint8_t synchronous[0x80];
	uint8_t irq[0x80];
	uint8_t fiq[0x80];
	uint8_t serror[0x80];
};

FERRO_PACKED_STRUCT(fint_vector_table) {
	fint_vector_table_block_t current_with_sp0;
	fint_vector_table_block_t current_with_spx;
	fint_vector_table_block_t lower_with_aarch64;
	fint_vector_table_block_t lower_with_aarch32;
};

FERRO_PACKED_STRUCT(fint_exception_frame) {
	uint64_t x0;
	uint64_t x1;
	uint64_t x2;
	uint64_t x3;
	uint64_t x4;
	uint64_t x5;
	uint64_t x6;
	uint64_t x7;
	uint64_t x8;
	uint64_t x9;
	uint64_t x10;
	uint64_t x11;
	uint64_t x12;
	uint64_t x13;
	uint64_t x14;
	uint64_t x15;
	uint64_t x16;
	uint64_t x17;
	uint64_t x18;
	uint64_t fp;
	uint64_t lr;
	uint64_t elr;
	uint64_t esr;
	uint64_t far;
};

FERRO_ENUM(uint8_t, fint_esr_code) {
	fint_esr_code_svc64                      = 0x15,
	fint_esr_code_instruction_abort_lower_el = 0x20,
	fint_esr_code_instruction_abort_same_el  = 0x21,
	fint_esr_code_pc_alignment_fault         = 0x22,
	fint_esr_code_data_abort_lower_el        = 0x24,
	fint_esr_code_data_abort_same_el         = 0x25,
	fint_esr_code_sp_alignment_fault         = 0x26,
	fint_esr_code_serror                     = 0x2f,
	fint_esr_code_breakpoint_lower_el        = 0x40,
	fint_esr_code_breakpoint_same_el         = 0x41,
	fint_esr_code_software_step_lower_el     = 0x32,
	fint_esr_code_software_step_same_el      = 0x33,
	fint_esr_code_watchpoint_lower_el        = 0x34,
	fint_esr_code_watchpoint_same_el         = 0x35,
	fint_esr_code_brk                        = 0x3c,
};

FERRO_STRUCT(fint_handler_common_data) {
	fint_state_t interrupt_state;
};

extern fint_vector_table_t fint_ivt;

FERRO_ALWAYS_INLINE fint_esr_code_t code_from_esr(uint64_t esr) {
	return (esr & (0x3fULL << 26)) >> 26;
};

static void fint_handler_common_begin(fint_handler_common_data_t* data) {
	// ARM automatically disables interrupts when handling an interrupt
	// so we need to let our interrupt management code know this
	data->interrupt_state = FARCH_PER_CPU(outstanding_interrupt_disable_count);
	FARCH_PER_CPU(outstanding_interrupt_disable_count) = 1;
};

static void fint_handler_common_end(fint_handler_common_data_t* data) {
	FARCH_PER_CPU(outstanding_interrupt_disable_count) = data->interrupt_state;
};

void fint_handler_current_with_spx_sync(fint_exception_frame_t* frame) {
	fint_handler_common_data_t data;
	fint_esr_code_t code;

	fint_handler_common_begin(&data);

	code = code_from_esr(frame->esr);

	switch (code) {
		case fint_esr_code_svc64: {
			fconsole_logf("received an SVC from the kernel at %p\n", (void*)(frame->elr - 4));
		} break;

		case fint_esr_code_instruction_abort_same_el: {
			fconsole_logf("instruction abort at %p on address %p\n", (void*)frame->elr, (void*)frame->far);
			fpanic();
		} break;

		case fint_esr_code_data_abort_same_el: {
			fconsole_logf("instruction abort at %p on address %p\n", (void*)frame->elr, (void*)frame->far);
			fpanic();
		} break;

		case fint_esr_code_brk:
		case fint_esr_code_breakpoint_same_el: {
			fconsole_logf("breakpoint at %p\n", (void*)frame->elr);
			frame->elr += 4;
		} break;

		case fint_esr_code_software_step_same_el: {
			fconsole_logf("software step at %p\n", (void*)frame->elr);
			frame->elr += 4;
		} break;

		case fint_esr_code_watchpoint_same_el: {
			fconsole_logf("watchpoint hit at %p on address %p\n", (void*)frame->elr, (void*)frame->far);
			frame->elr += 4;
		} break;

		// since we don't have userspace implemented yet, these should never occur
		case fint_esr_code_instruction_abort_lower_el:
		case fint_esr_code_data_abort_lower_el:
		case fint_esr_code_breakpoint_lower_el:
		case fint_esr_code_software_step_lower_el:
		case fint_esr_code_watchpoint_lower_el:

		// well, crap, we don't know what this is about! just die.
		default: {
			fconsole_log("received invalid synchronous exception!\n");
			fpanic();
		} break;
	}

	fint_handler_common_end(&data);
};

void fint_handler_current_with_spx_irq(fint_exception_frame_t* frame) {
	fint_handler_common_data_t data;

	fint_handler_common_begin(&data);

	fconsole_log("got an IRQ\n");

	fint_handler_common_end(&data);
};

void fint_handler_current_with_spx_fiq(fint_exception_frame_t* frame) {
	fint_handler_common_data_t data;

	fint_handler_common_begin(&data);

	fconsole_log("got an FIQ\n");

	fint_handler_common_end(&data);
};

void fint_handler_current_with_spx_serror(fint_exception_frame_t* frame) {
	fint_handler_common_data_t data;
	fint_handler_common_begin(&data);

	// SErrors are generally unrecoverable, so just die
	fpanic();

	// unnecessary, but just for consistency
	fint_handler_common_end(&data);
};

void fint_handler_current_with_sp0_sync(fint_exception_frame_t* frame) {
	return fint_handler_current_with_spx_sync(frame);
};

void fint_handler_current_with_sp0_irq(fint_exception_frame_t* frame) {
	return fint_handler_current_with_spx_irq(frame);
};

void fint_handler_current_with_sp0_fiq(fint_exception_frame_t* frame) {
	return fint_handler_current_with_spx_fiq(frame);
};

void fint_handler_current_with_sp0_serror(fint_exception_frame_t* frame) {
	return fint_handler_current_with_spx_serror(frame);
};

void fint_init(void) {
	__asm__ volatile("msr VBAR_EL1, %0" :: "r" (&fint_ivt) : "memory");

	fint_enable();

	// test: hit a breakpoint
	//__builtin_debugtrap();
};
