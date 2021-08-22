#include <ferro/core/threads.private.h>
#include <ferro/core/paging.h>
#include <ferro/core/x86_64/interrupts.h>
#include <libk/libk.h>
#include <ferro/core/mempool.h>
#include <ferro/core/panic.h>

#include <stdatomic.h>

void farch_threads_runner(void);

ferr_t fthread_new(fthread_initializer_f initializer, void* data, void* stack_base, size_t stack_size, fthread_flags_t flags, fthread_t** out_thread) {
	fthread_private_t* new_thread = NULL;
	bool release_stack_on_fail = false;

	if (!initializer || !out_thread) {
		return ferr_invalid_argument;
	}

	if (!stack_base) {
		if (fpage_allocate_kernel(fpage_round_up_to_page_count(stack_size), &stack_base) != ferr_ok) {
			return ferr_temporary_outage;
		}

		release_stack_on_fail = true;
		flags |= fthread_flag_deallocate_stack_on_exit;
	}

	if (fmempool_allocate(sizeof(fthread_private_t), NULL, (void**)&new_thread) != ferr_ok) {
		if (release_stack_on_fail) {
			if (fpage_free_kernel(stack_base, fpage_round_up_to_page_count(stack_size)) != ferr_ok) {
				fpanic("Failed to free thread stack");
			}
		}
		return ferr_temporary_outage;
	}

	*out_thread = (void*)new_thread;

	// clear the thread
	memset(new_thread, 0, sizeof(*new_thread));

	flock_spin_intsafe_init(&new_thread->thread.lock);

	new_thread->thread.reference_count = 1;

	new_thread->thread.stack_base = stack_base;
	new_thread->thread.stack_size = stack_size;

	new_thread->thread.flags = flags;

	// the thread must start as suspended
	fthread_state_execution_write_locked(&new_thread->thread, fthread_state_execution_suspended);

	new_thread->thread.saved_context.rip = (uintptr_t)farch_threads_runner;
	new_thread->thread.saved_context.rsp = (uintptr_t)new_thread->thread.stack_base + new_thread->thread.stack_size;
	new_thread->thread.saved_context.rdi = (uintptr_t)data;
	new_thread->thread.saved_context.r10 = (uintptr_t)initializer;
	new_thread->thread.saved_context.cs = farch_int_gdt_index_code * 8;
	new_thread->thread.saved_context.ss = farch_int_gdt_index_data * 8;

	// set the reserved bit (bit 1) and the interrupt-enable bit (bit 9)
	new_thread->thread.saved_context.rflags = (1ULL << 1) | (1ULL << 9);

	return ferr_ok;
};

fthread_t* fthread_current(void) {
	return FARCH_PER_CPU(current_thread);
};
