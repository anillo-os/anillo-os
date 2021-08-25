#include <ferro/core/threads.private.h>
#include <ferro/core/paging.h>
#include <ferro/core/x86_64/interrupts.h>
#include <libk/libk.h>
#include <ferro/core/mempool.h>
#include <ferro/core/panic.h>

#include <stdatomic.h>

void farch_threads_runner(void);

void farch_thread_init_info(fthread_t* thread, fthread_initializer_f initializer, void* data) {
	thread->saved_context.rip = (uintptr_t)farch_threads_runner;
	thread->saved_context.rsp = (uintptr_t)thread->stack_base + thread->stack_size;
	thread->saved_context.rdi = (uintptr_t)data;
	thread->saved_context.r10 = (uintptr_t)initializer;
	thread->saved_context.cs = farch_int_gdt_index_code * 8;
	thread->saved_context.ss = farch_int_gdt_index_data * 8;

	// set the reserved bit (bit 1) and the interrupt-enable bit (bit 9)
	thread->saved_context.rflags = (1ULL << 1) | (1ULL << 9);
};

fthread_t* fthread_current(void) {
	return FARCH_PER_CPU(current_thread);
};
