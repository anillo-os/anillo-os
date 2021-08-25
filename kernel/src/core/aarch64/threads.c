#include <ferro/core/threads.private.h>
#include <ferro/core/aarch64/per-cpu.h>

void farch_threads_runner(void);

void farch_thread_init_info(fthread_t* thread, fthread_initializer_f initializer, void* data) {
	thread->saved_context.pc = (uintptr_t)farch_threads_runner;
	thread->saved_context.sp = (uintptr_t)thread->stack_base + thread->stack_size;
	thread->saved_context.x0 = (uintptr_t)data;
	thread->saved_context.x19 = (uintptr_t)initializer;

	// leave the DAIF mask bits cleared to enable interrupts
	thread->saved_context.pstate = farch_thread_pstate_aarch64 | farch_thread_pstate_el1 | farch_thread_pstate_sp0;
};

fthread_t* fthread_current(void) {
	return FARCH_PER_CPU(current_thread);
};
