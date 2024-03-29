/*
 * This file is part of Anillo OS
 * Copyright (C) 2021 Anillo OS Developers
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

#include <ferro/userspace/threads.private.h>
#include <ferro/core/threads.private.h>
#include <ferro/core/panic.h>
#include <ferro/core/mempool.h>
#include <ferro/core/ghmap.h>
#include <ferro/core/locks.h>
#include <ferro/core/workers.h>
#include <libsimple/libsimple.h>
#include <ferro/core/paging.private.h>
#include <ferro/userspace/futex.h>
#include <ferro/userspace/processes.h>
#include <ferro/core/console.h>
#include <ferro/userspace/uio.h>

#if FERRO_ARCH == FERRO_ARCH_x86_64
	#include <ferro/core/x86_64/xsave.h>
#endif

// DA7A == Data
// (because the hook is only used to swap address spaces)
// UPDATE:
// the hook is now also used to swap TLS addresses. same thing, though; still just data.
#define UTHREAD_HOOK_OWNER_ID (0xDA7Aull)

// uses a thread pointer as the key (and key_size can be anything; we don't use it)
static simple_ghmap_t uthread_map;

// TODO: this should probably be an rwlock instead (once we get those)
static flock_mutex_t uthread_map_mutex = FLOCK_MUTEX_INIT;

static simple_ghmap_hash_t simple_ghmap_hash_thread(void* context, const void* key, size_t key_size) {
	// we can use the thread's pointer as its hash key
	return (uintptr_t)key;
};

futhread_data_t* futhread_data_for_thread(fthread_t* thread) {
	fthread_private_t* private_thread = (void*)thread;
	futhread_data_t* data = NULL;
	uint8_t slot = fthread_find_hook(thread, UTHREAD_HOOK_OWNER_ID);

	if (slot == UINT8_MAX) {
		data = NULL;
	} else {
		data = private_thread->hooks[slot].context;
	}

	return data;
};

void futhread_init(void) {
	fpanic_status(simple_ghmap_init(&uthread_map, 0, sizeof(futhread_data_private_t), simple_ghmap_allocate_mempool, simple_ghmap_free_mempool, simple_ghmap_hash_thread, NULL, NULL, NULL, NULL, NULL));

	futhread_arch_init();
};

static void uthread_thread_died(void* context) {
	fthread_t* thread = context;
	futhread_data_t* data = futhread_data_for_thread(thread);
	futhread_data_private_t* private_data = (void*)data;

	// we're guaranteed to be called in a thread context, so we can operate normally here

	if (!data) {
		// huh, it's not there. oh well.
		return;
	}

	// notify the death futex (if we have one)
	if (private_data->uthread_death_futex) {
		// first, store the desired value
		fpage_space_t* curr = fpage_space_current();
		fpanic_status(fpage_space_swap(data->user_space));
		// FIXME: we should not access userspace memory directly here.
		//        we need to have a set of functions to access userspace memory safely, without fear of faulting.
		if (fpage_space_virtual_to_physical(data->user_space, private_data->uthread_death_futex->address) != UINTPTR_MAX) {
			FERRO_WUR_IGNORE(ferro_uio_atomic_store_8_relaxed(private_data->uthread_death_futex->address, private_data->uthread_death_futex_value));
		}
		fpanic_status(fpage_space_swap(curr));

		// next, wake up anyone waiting on the futex
		fwaitq_wake_many(&private_data->uthread_death_futex->waitq, SIZE_MAX);

		// finally, release the futex
		futex_release(private_data->uthread_death_futex);
		private_data->uthread_death_futex = NULL;
		private_data->uthread_death_futex_value = 0;
	}

	if ((data->flags & futhread_flag_deallocate_user_stack_on_exit) != 0) {
		fpanic_status(fpage_space_free(data->user_space, data->user_stack_base, fpage_round_up_to_page_count(data->user_stack_size)));
	}

	if ((data->flags & futhread_flag_destroy_address_space_on_exit) != 0) {
		fpage_space_destroy(data->user_space);
	}

	if ((data->flags & futhread_flag_deallocate_address_space_on_exit) != 0) {
		fpanic_status(fmempool_free(data->user_space));
	}

	flock_mutex_lock(&private_data->signals_mutex);

	simple_ghmap_destroy(&private_data->signal_handler_table);

	// clean up the signal queues

	futhread_pending_signal_t* next = NULL;
	for (futhread_pending_signal_t* signal = private_data->current_signal; signal != NULL; signal = next) {
		next = signal->next;

		if (signal->was_blocked) {
			FERRO_WUR_IGNORE(fthread_unblock(signal->target_uthread));
		}

		FERRO_WUR_IGNORE(fmempool_free(signal));
	}
	private_data->current_signal = NULL;

	next = NULL;
	for (futhread_pending_signal_t* signal = private_data->pending_signal; signal != NULL; signal = next) {
		next = signal->next;

		if (signal->was_blocked) {
			FERRO_WUR_IGNORE(fthread_unblock(signal->target_uthread));
		}

		FERRO_WUR_IGNORE(fmempool_free(signal));
	}
	private_data->pending_signal = NULL;
	private_data->last_pending_signal = NULL;

	flock_mutex_unlock(&private_data->signals_mutex);

	fwaitq_wake_many(&data->death_wait, SIZE_MAX);
};

static void uthread_thread_destroyed(void* context) {
	fthread_t* thread = context;
	futhread_data_t* data = futhread_data_for_thread(thread);

	fwaitq_wake_many(&data->destroy_wait, SIZE_MAX);

	FERRO_WUR_IGNORE(fmempool_free(data->saved_syscall_context));

	flock_mutex_lock(&uthread_map_mutex);
	fpanic_status(simple_ghmap_clear(&uthread_map, thread, 0));
	flock_mutex_unlock(&uthread_map_mutex);
};

static ferr_t uthread_ending_interrupt(void* context, fthread_t* thread) {
	futhread_data_t* data = context;
	futhread_ending_interrupt_arch(thread, data);
	return ferr_ok;
};

FERRO_STRUCT(uthread_signal_iterator_context) {
	fthread_t* target_uthread;
	futhread_special_signal_t special_signal;
};

static bool uthread_signal_iterator(void* _context, fproc_t* process, fthread_t* uthread) {
	uthread_signal_iterator_context_t* context = _context;
	ferr_t status = ferr_ok;

	if (uthread == context->target_uthread) {
		// skip this uthread
		return true;
	}

	if (futhread_signal_special(uthread, context->special_signal, context->target_uthread, 0) == ferr_ok) {
		return false;
	}

	return true;
};

#define SPECIAL_SIGNAL_WORKER(_special_signal) \
	static void uthread_ ## _special_signal ## _worker(void* context) { \
		fthread_t* uthread = context; \
		fproc_t* process = futhread_process(uthread); \
		\
		ferr_t status = futhread_signal_special(uthread, futhread_special_signal_ ## _special_signal, uthread, 0); \
		\
		if (status != ferr_ok && process) { \
			/* try to see if another thread in the process can handle it */ \
			uthread_signal_iterator_context_t iterator_context = { \
				.target_uthread = uthread, \
				.special_signal = futhread_special_signal_ ## _special_signal, \
			}; \
			if (fproc_for_each_thread(process, uthread_signal_iterator, &iterator_context) == ferr_cancelled) { \
				status = ferr_ok; \
			} \
		} \
		\
		if (status != ferr_ok) { \
			/* kill the target thread/process */ \
			if (process) { \
				fproc_kill(process); \
			} else { \
				FERRO_WUR_IGNORE(fthread_kill(uthread)); \
			} \
			fconsole_logf("killed thread/process because of special signal " #_special_signal "\n"); \
		} \
		\
		/* remove our block; the signal should have placed a block of its own */ \
		FERRO_WUR_IGNORE(fthread_unblock(uthread)); \
		fthread_release(uthread); \
	};

SPECIAL_SIGNAL_WORKER(bus_error);
SPECIAL_SIGNAL_WORKER(page_fault);
SPECIAL_SIGNAL_WORKER(floating_point_exception);
SPECIAL_SIGNAL_WORKER(illegal_instruction);
SPECIAL_SIGNAL_WORKER(debug);
SPECIAL_SIGNAL_WORKER(division_by_zero);

static ferr_t uthread_bus_error(void* context, fthread_t* thread, void* address) {
	if (fint_frame_is_kernel_space(fint_current_frame())) {
		return ferr_unsupported;
	}

	futhread_data_private_t* private_data = (void*)futhread_data_for_thread(thread);
	FERRO_WUR_IGNORE(fthread_retain(thread));
	FERRO_WUR_IGNORE(fthread_block(thread, false));
	private_data->faulted_memory_address = address;
	FERRO_WUR_IGNORE(fwork_schedule_new(uthread_bus_error_worker, thread, 0, NULL));
	return ferr_permanent_outage;
};

static ferr_t uthread_page_fault(void* context, fthread_t* thread, void* address) {
	if (fint_frame_is_kernel_space(fint_current_frame())) {
		return ferr_unsupported;
	}

	// DEBUGGING
	fconsole_logf("Faulted on %p\n", address);
	fint_log_frame(fint_current_frame());
	fint_trace_interrupted_stack(fint_current_frame());

	futhread_data_private_t* private_data = (void*)futhread_data_for_thread(thread);
	FERRO_WUR_IGNORE(fthread_retain(thread));
	FERRO_WUR_IGNORE(fthread_block(thread, false));
	private_data->faulted_memory_address = address;

	FERRO_WUR_IGNORE(fwork_schedule_new(uthread_page_fault_worker, thread, 0, NULL));
	return ferr_permanent_outage;
};

static ferr_t uthread_floating_point_exception(void* context, fthread_t* thread) {
	if (fint_frame_is_kernel_space(fint_current_frame())) {
		return ferr_unsupported;
	}

	futhread_data_private_t* private_data = (void*)futhread_data_for_thread(thread);
	FERRO_WUR_IGNORE(fthread_retain(thread));
	FERRO_WUR_IGNORE(fthread_block(thread, false));
	FERRO_WUR_IGNORE(fwork_schedule_new(uthread_floating_point_exception_worker, thread, 0, NULL));
	return ferr_permanent_outage;
};

static ferr_t uthread_illegal_instruction(void* context, fthread_t* thread) {
	if (fint_frame_is_kernel_space(fint_current_frame())) {
		return ferr_unsupported;
	}

	futhread_data_private_t* private_data = (void*)futhread_data_for_thread(thread);
	FERRO_WUR_IGNORE(fthread_retain(thread));
	FERRO_WUR_IGNORE(fthread_block(thread, false));

	// DEBUGGING
	fint_trace_interrupted_stack(fint_current_frame());

	FERRO_WUR_IGNORE(fwork_schedule_new(uthread_illegal_instruction_worker, thread, 0, NULL));
	return ferr_permanent_outage;
};

static ferr_t uthread_debug_trap(void* context, fthread_t* thread) {
	if (fint_frame_is_kernel_space(fint_current_frame())) {
		return ferr_unsupported;
	}

	futhread_data_private_t* private_data = (void*)futhread_data_for_thread(thread);
	FERRO_WUR_IGNORE(fthread_retain(thread));
	FERRO_WUR_IGNORE(fthread_block(thread, false));
	FERRO_WUR_IGNORE(fwork_schedule_new(uthread_debug_worker, thread, 0, NULL));
	return ferr_permanent_outage;
};

static ferr_t uthread_division_by_zero(void* context, fthread_t* thread) {
	if (fint_frame_is_kernel_space(fint_current_frame())) {
		return ferr_unsupported;
	}

	futhread_data_private_t* private_data = (void*)futhread_data_for_thread(thread);
	FERRO_WUR_IGNORE(fthread_retain(thread));
	FERRO_WUR_IGNORE(fthread_block(thread, false));
	FERRO_WUR_IGNORE(fwork_schedule_new(uthread_division_by_zero_worker, thread, 0, NULL));
	return ferr_permanent_outage;
};

static const fthread_hook_callbacks_t hook_callbacks = {
	.ending_interrupt = uthread_ending_interrupt,
	.bus_error = uthread_bus_error,
	.page_fault = uthread_page_fault,
	.floating_point_exception = uthread_floating_point_exception,
	.illegal_instruction = uthread_illegal_instruction,
	.debug_trap = uthread_debug_trap,
	.division_by_zero = uthread_division_by_zero,
};

#if FERRO_ARCH == FERRO_ARCH_x86_64
	#define FTHREAD_EXTRA_SAVE_SIZE FARCH_PER_CPU(xsave_area_size)
#elif FERRO_ARCH == FERRO_ARCH_aarch64
	#define FTHREAD_EXTRA_SAVE_SIZE 0
#endif

ferr_t futhread_register(fthread_t* thread, void* user_stack_base, size_t user_stack_size, fpage_space_t* user_space, futhread_flags_t flags, futhread_syscall_handler_f syscall_handler, void* syscall_handler_context) {
	futhread_data_t* data = NULL;
	futhread_data_private_t* private_data = NULL;
	bool created = false;
	bool clear_uthread_on_fail = false;
	bool deallocate_space_on_fail = false;
	bool destroy_space_on_fail = false;
	bool release_stack_on_fail = false;
	bool clear_flag_on_fail = false;
	ferr_t status = ferr_ok;
	fthread_private_t* private_thread = (void*)thread;
	bool destroy_signal_handler_table_on_fail = false;

retry_lookup:
	if (fthread_is_uthread(thread)) {
		status = ferr_already_in_progress;
		goto out_unlocked;
	}

	flock_mutex_lock(&uthread_map_mutex);

	if (simple_ghmap_lookup(&uthread_map, thread, 0, true, SIZE_MAX, &created, (void*)&data, NULL) != ferr_ok) {
		status = ferr_temporary_outage;
		goto out_locked;
	}

	private_data = (void*)data;

	if (!created) {
		// if this happens, it means the new thread has the same address as an old uthread that hasn't been cleared from the hashmap yet.
		// just try again until we're good.
		flock_mutex_unlock(&uthread_map_mutex);
		goto retry_lookup;
	}

	private_data->process = NULL;
	data->saved_syscall_context = NULL;

	clear_uthread_on_fail = true;

	if (!user_space) {
		if (fmempool_allocate(sizeof(fpage_space_t), NULL, (void*)&user_space) != ferr_ok) {
			status = ferr_temporary_outage;
			goto out_locked;
		}
		deallocate_space_on_fail = true;
		flags |= futhread_flag_deallocate_address_space_on_exit;

		if (fpage_space_init(user_space) != ferr_ok) {
			status = ferr_temporary_outage;
			goto out_locked;
		}
		destroy_space_on_fail = true;
		flags |= futhread_flag_destroy_address_space_on_exit;
	}

	data->user_space = user_space;

	if (!user_stack_base) {
		if (fpage_space_allocate(data->user_space, fpage_round_up_to_page_count(user_stack_size), &user_stack_base, fpage_flag_unprivileged) != ferr_ok) {
			status = ferr_temporary_outage;
			goto out_locked;
		}

		release_stack_on_fail = true;
		flags |= futhread_flag_deallocate_user_stack_on_exit;
	}

	data->flags = flags;
	data->user_stack_base = user_stack_base;
	data->user_stack_size = user_stack_size;

	// register a waiter to clear the uthread data when the thread dies
	fwaitq_waiter_init(&data->thread_death_waiter, uthread_thread_died, thread);
	fwaitq_wait(&thread->death_wait, &data->thread_death_waiter);

	fwaitq_waiter_init(&data->thread_destruction_waiter, uthread_thread_destroyed, thread);
	fwaitq_wait(&thread->destroy_wait, &data->thread_destruction_waiter);

	fwaitq_init(&data->death_wait);
	fwaitq_init(&data->destroy_wait);

	flock_spin_intsafe_lock(&thread->lock);
	thread->flags |= fthread_private_flag_has_userspace;
	clear_flag_on_fail = true;
	flock_spin_intsafe_unlock(&thread->lock);

	status = fmempool_allocate_advanced(sizeof(*data->saved_syscall_context) + FTHREAD_EXTRA_SAVE_SIZE, fpage_round_up_to_alignment_power(64), UINT8_MAX, 0, NULL, (void*)&data->saved_syscall_context);
	if (status != ferr_ok) {
		goto out_locked;
	}

	simple_memset(data->saved_syscall_context, 0, sizeof(*data->saved_syscall_context) + FTHREAD_EXTRA_SAVE_SIZE);

#if FERRO_ARCH == FERRO_ARCH_x86_64
	farch_xsave_area_legacy_t* xsave_legacy;

	data->saved_syscall_context->rsp = (uintptr_t)data->user_stack_base + data->user_stack_size;
	data->saved_syscall_context->cs = (farch_int_gdt_index_code_user * 8) | 3;
	data->saved_syscall_context->ss = (farch_int_gdt_index_data_user * 8) | 3;

	// set the reserved bit (bit 1) and the interrupt-enable bit (bit 9)
	data->saved_syscall_context->rflags = (1ULL << 1) | (1ULL << 9);

	// initialize MXCSR
	xsave_legacy = (void*)data->saved_syscall_context->xsave_area;
	xsave_legacy->mxcsr = 0x1f80ull | (0xffbfull << 32); // TODO: programmatically determine the xsave mask
#elif FERRO_ARCH == FERRO_ARCH_aarch64
	data->saved_syscall_context->sp = (uintptr_t)data->user_stack_base + data->user_stack_size;

	// leave the DAIF mask bits cleared to enable interrupts
	data->saved_syscall_context->pstate = farch_thread_pstate_aarch64 | farch_thread_pstate_el0 | farch_thread_pstate_sp0;
#endif

	if (fthread_register_hook(thread, UTHREAD_HOOK_OWNER_ID, data, &hook_callbacks) == UINT8_MAX) {
		status = ferr_temporary_outage;
		goto out_locked;
	}

	data->syscall_handler = syscall_handler;
	data->syscall_handler_context = syscall_handler_context;

	private_data->thread = thread;

	private_data->uthread_death_futex = NULL;
	private_data->uthread_death_futex_value = 0;

	status = simple_ghmap_init(&private_data->signal_handler_table, 16, 0, simple_ghmap_allocate_mempool, simple_ghmap_free_mempool, NULL, NULL, NULL, NULL, NULL, NULL);
	if (status != ferr_ok) {
		goto out_locked;
	}

	destroy_signal_handler_table_on_fail = true;

	private_data->pending_signal = NULL;
	private_data->last_pending_signal = NULL;
	private_data->current_signal = NULL;

	flock_mutex_init(&private_data->signals_mutex);

	private_data->use_fake_interrupt_return = false;

	simple_memset(&private_data->signal_mapping, 0, sizeof(private_data->signal_mapping));

	simple_memset(&private_data->signal_stack, 0, sizeof(private_data->signal_stack));
	private_data->signal_mask = 0;

	futhread_arch_init_private_data(private_data);

out_locked:
	if (status != ferr_ok) {
		if (destroy_signal_handler_table_on_fail) {
			simple_ghmap_destroy(&private_data->signal_handler_table);
		}
		if (data && data->saved_syscall_context) {
			FERRO_WUR_IGNORE(fmempool_free(data->saved_syscall_context));
		}
		if (release_stack_on_fail) {
			fpanic_status(fpage_space_free(data->user_space, user_stack_base, fpage_round_up_to_page_count(user_stack_size)));
		}
		if (destroy_space_on_fail) {
			fpage_space_destroy(user_space);
		}
		if (deallocate_space_on_fail) {
			FERRO_WUR_IGNORE(fmempool_free(user_space));
		}
		if (clear_uthread_on_fail) {
			fpanic_status(simple_ghmap_clear(&uthread_map, thread, 0));
		}
		if (clear_flag_on_fail) {
			flock_spin_intsafe_lock(&thread->lock);
			thread->flags &= ~fthread_private_flag_has_userspace;
			flock_spin_intsafe_unlock(&thread->lock);
		}
	} else if (thread == fthread_current()) {
		fpanic_status(fpage_space_swap(data->user_space));
	}

	flock_mutex_unlock(&uthread_map_mutex);

out_unlocked:
	return status;
};

ferr_t futhread_jump_user(fthread_t* uthread, void* address) {
	futhread_data_t* data = NULL;

	if (!uthread || !address) {
		return ferr_invalid_argument;
	}

	data = futhread_data_for_thread(uthread);

	if (!data) {
		return ferr_invalid_argument;
	}

	// make sure the address is valid
	// TODO: make sure it's executable and unprivileged
	//if (fpage_space_virtual_to_physical(data->user_space, (uintptr_t)address) == UINTPTR_MAX) {
	//	return ferr_invalid_argument;
	//}

	fpanic_status(fpage_space_swap(data->user_space));

	if (uthread == futhread_current()) {
		futhread_jump_user_self_arch(uthread, data, address);
	} else {
		// TODO: support threads other than the current one
		return ferr_unsupported;
	}
};

void futhread_jump_user_self(void* address) {
	fpanic_status(futhread_jump_user(futhread_current(), address));
	__builtin_unreachable();
};

ferr_t futhread_space(fthread_t* uthread, fpage_space_t** out_space) {
	futhread_data_t* data = NULL;

	if (!uthread || !out_space) {
		return ferr_invalid_argument;
	}

	data = futhread_data_for_thread(uthread);

	if (!data) {
		return ferr_invalid_argument;
	}

	*out_space = data->user_space;

	return ferr_ok;
};

ferr_t futhread_context(fthread_t* uthread, fthread_saved_context_t** out_saved_user_context) {
	futhread_data_t* data = NULL;

	if (!uthread || !out_saved_user_context) {
		return ferr_invalid_argument;
	}

	data = futhread_data_for_thread(uthread);

	if (!data) {
		return ferr_invalid_argument;
	}

	*out_saved_user_context = data->saved_syscall_context;

	return ferr_ok;
};

bool fthread_is_uthread(fthread_t* thread) {
	bool result = false;
	flock_spin_intsafe_lock(&thread->lock);
	result = (thread->flags & fthread_private_flag_has_userspace) != 0;
	flock_spin_intsafe_unlock(&thread->lock);
	return result;
};

fthread_t* futhread_current(void) {
	fthread_t* current = fthread_current();
	return fthread_is_uthread(current) ? current : NULL;
};

fproc_t* futhread_process(fthread_t* uthread) {
	futhread_data_t* data = futhread_data_for_thread(uthread);
	futhread_data_private_t* private_data = (futhread_data_private_t*)data;

	if (!data) {
		return NULL;
	}

	return private_data->process;
};

// must be holding the signal mutex
static void futhread_signal_setup_context(fsyscall_signal_stack_t* signal_stack, futhread_pending_signal_t* signal, fthread_saved_context_t* context_to_save, fthread_saved_context_t* context, uint64_t* signal_mask) {
	// first, find the appropriate initial stack pointer to use
	void* stack_pointer;
	bool reused = false;
	fsyscall_signal_info_t* signal_info;
#if FERRO_ARCH == FERRO_ARCH_x86_64
	void* xsave_area;
#elif FERRO_ARCH == FERRO_ARCH_aarch64
	void* fp_regs;
#endif

	if (signal_stack->base) {
		void* old_sp;

		stack_pointer = (char*)signal_stack->base + signal_stack->size;

		// check if this stack is the one we were just using
#if FERRO_ARCH == FERRO_ARCH_x86_64
		old_sp = (void*)context_to_save->rsp;
#elif FERRO_ARCH == FERRO_ARCH_aarch64
		old_sp = (void*)context_to_save->sp;
#endif

		if (old_sp > signal_stack->base && old_sp < stack_pointer) {
			// start from the old_sp instead, since this stack is already in-use
			stack_pointer = old_sp;
			reused = true;
		}

		if (signal_stack->flags & fsyscall_signal_stack_flag_clear_on_use) {
			simple_memset(signal_stack, 0, sizeof(*signal_stack));
		}
	} else {
		// use the stack pointer we were just using, but skip the red zone
#if FERRO_ARCH == FERRO_ARCH_x86_64
		stack_pointer = (void*)context_to_save->rsp;
#elif FERRO_ARCH == FERRO_ARCH_aarch64
		stack_pointer = (void*)context_to_save->sp;
#endif
		reused = true;
	}

	if (reused) {
		// if a stack is being re-used (i.e. we're using a stack that's already in-use,
		// either by another signal handler or the thread itself), we need to leave space
		// to avoid clobbering the red zone (which exists on both x86_64 and AARCH64)
		stack_pointer = (char*)stack_pointer - 128;
	}

#if FERRO_ARCH == FERRO_ARCH_x86_64
	// make space on the stack for the xsave area
	// (align it, too)
	stack_pointer = (void*)(((uintptr_t)stack_pointer - FARCH_PER_CPU(xsave_area_size)) & ~63);
	xsave_area = stack_pointer;
#elif FERRO_ARCH == FERRO_ARCH_aarch64
	// make space on the stack for the FP registers
	// (and align it to 16 bytes)
	stack_pointer = (void*)(((uintptr_t)stack_pointer - (sizeof(__uint128_t) * 32)) & ~15);
	fp_regs = stack_pointer;
#endif

	// make space on the stack for the signal info
	stack_pointer = (char*)stack_pointer - (sizeof(fsyscall_signal_info_t) + sizeof(ferro_thread_context_t));

	// TODO: verify that this address is valid before writing to it
	signal_info = stack_pointer;

	// align the stack to 16 bytes
	stack_pointer = (void*)((uintptr_t)stack_pointer & ~15);

	signal_info->flags = signal->was_blocked ? fsyscall_signal_info_flag_blocked : 0;
	signal_info->signal_number = signal->signal;
	signal_info->thread_id = signal->target_uthread->id;
	signal_info->thread_context = (void*)((char*)signal_info + sizeof(fsyscall_signal_info_t));
	signal_info->data = 0;
	signal_info->mask = *signal_mask;

	// mask the signal now, if asked to do so
	if (signal->signal < 64 && (signal->configuration.flags & fsyscall_signal_configuration_flag_mask_on_handle) != 0) {
		*signal_mask |= 1ull << signal->signal;
	}

#if FERRO_ARCH == FERRO_ARCH_x86_64
	signal_info->thread_context->rax = context_to_save->rax;
	signal_info->thread_context->rcx = context_to_save->rcx;
	signal_info->thread_context->rdx = context_to_save->rdx;
	signal_info->thread_context->rbx = context_to_save->rbx;
	signal_info->thread_context->rsi = context_to_save->rsi;
	signal_info->thread_context->rdi = context_to_save->rdi;
	signal_info->thread_context->rsp = context_to_save->rsp;
	signal_info->thread_context->rbp = context_to_save->rbp;
	signal_info->thread_context->r8 = context_to_save->r8;
	signal_info->thread_context->r9 = context_to_save->r9;
	signal_info->thread_context->r10 = context_to_save->r10;
	signal_info->thread_context->r11 = context_to_save->r11;
	signal_info->thread_context->r12 = context_to_save->r12;
	signal_info->thread_context->r13 = context_to_save->r13;
	signal_info->thread_context->r14 = context_to_save->r14;
	signal_info->thread_context->r15 = context_to_save->r15;
	signal_info->thread_context->rip = context_to_save->rip;
	signal_info->thread_context->rflags = context_to_save->rflags;
	signal_info->thread_context->xsave_area = xsave_area;
	signal_info->thread_context->xsave_area_size = FARCH_PER_CPU(xsave_area_size);

	// now copy the xsave area
	simple_memcpy(signal_info->thread_context->xsave_area, context_to_save->xsave_area, FARCH_PER_CPU(xsave_area_size));
#elif FERRO_ARCH == FERRO_ARCH_aarch64
	signal_info->thread_context->x0 = context_to_save->x0;
	signal_info->thread_context->x1 = context_to_save->x1;
	signal_info->thread_context->x2 = context_to_save->x2;
	signal_info->thread_context->x3 = context_to_save->x3;
	signal_info->thread_context->x4 = context_to_save->x4;
	signal_info->thread_context->x5 = context_to_save->x5;
	signal_info->thread_context->x6 = context_to_save->x6;
	signal_info->thread_context->x7 = context_to_save->x7;
	signal_info->thread_context->x8 = context_to_save->x8;
	signal_info->thread_context->x9 = context_to_save->x9;
	signal_info->thread_context->x10 = context_to_save->x10;
	signal_info->thread_context->x11 = context_to_save->x11;
	signal_info->thread_context->x12 = context_to_save->x12;
	signal_info->thread_context->x13 = context_to_save->x13;
	signal_info->thread_context->x14 = context_to_save->x14;
	signal_info->thread_context->x15 = context_to_save->x15;
	signal_info->thread_context->x16 = context_to_save->x16;
	signal_info->thread_context->x17 = context_to_save->x17;
	signal_info->thread_context->x18 = context_to_save->x18;
	signal_info->thread_context->x19 = context_to_save->x19;
	signal_info->thread_context->x20 = context_to_save->x20;
	signal_info->thread_context->x21 = context_to_save->x21;
	signal_info->thread_context->x22 = context_to_save->x22;
	signal_info->thread_context->x23 = context_to_save->x23;
	signal_info->thread_context->x24 = context_to_save->x24;
	signal_info->thread_context->x25 = context_to_save->x25;
	signal_info->thread_context->x26 = context_to_save->x26;
	signal_info->thread_context->x27 = context_to_save->x27;
	signal_info->thread_context->x28 = context_to_save->x28;
	signal_info->thread_context->x29 = context_to_save->x29;
	signal_info->thread_context->x30 = context_to_save->x30;
	signal_info->thread_context->pc = context_to_save->pc;
	signal_info->thread_context->sp = context_to_save->sp;
	signal_info->thread_context->pstate = context_to_save->pstate;
	signal_info->thread_context->fpsr = context_to_save->fpsr;
	signal_info->thread_context->fpcr = context_to_save->fpcr;
	signal_info->thread_context->fp_registers = fp_regs;

	// now copy the FP registers
	simple_memcpy(fp_regs, context_to_save->fp_registers, sizeof(context_to_save->fp_registers));
#endif

	// zero out the context
	simple_memset(context, 0, sizeof(*context) + FTHREAD_EXTRA_SAVE_SIZE);

	// and initialize architecture-specific data
#if FERRO_ARCH == FERRO_ARCH_x86_64
	farch_xsave_area_legacy_t* xsave_legacy;

	context->rip = (uintptr_t)signal->configuration.handler;
	context->rsp = (uintptr_t)stack_pointer;
	context->rdi = (uintptr_t)signal->configuration.context;
	context->rsi = (uintptr_t)signal_info;
	context->cs = (farch_int_gdt_index_code_user * 8) | 3;
	context->ss = (farch_int_gdt_index_data_user * 8) | 3;

	// set the reserved bit (bit 1) and the interrupt-enable bit (bit 9)
	context->rflags = (1ULL << 1) | (1ULL << 9);

	// initialize MXCSR
	xsave_legacy = (void*)context->xsave_area;
	xsave_legacy->mxcsr = 0x1f80ull | (0xffbfull << 32); // TODO: programmatically determine the xsave mask
#elif FERRO_ARCH == FERRO_ARCH_aarch64
	context->pc = (uintptr_t)signal->configuration.handler;
	context->sp = (uintptr_t)stack_pointer;
	context->x0 = (uintptr_t)signal->configuration.context;
	context->x1 = (uintptr_t)signal_info;

	// leave the DAIF mask bits cleared to enable interrupts
	context->pstate = farch_thread_pstate_aarch64 | farch_thread_pstate_el0 | farch_thread_pstate_sp0;
#endif
};

// must be called with the signal mutex held
// returns with the signal mutex dropped
static ferr_t futhread_signal_internal(fthread_t* uthread, futhread_pending_signal_t* signal, fthread_t* target_uthread, futhread_signal_flags_t flags) {
	futhread_data_t* data = futhread_data_for_thread(uthread);
	futhread_data_private_t* private_data = (void*)data;
	futhread_data_t* target_data = futhread_data_for_thread(target_uthread);
	futhread_data_private_t* target_private_data = (void*)target_data;
	ferr_t status = ferr_ok;
	futhread_signal_handler_t* handler = NULL;
	bool block_self = false;
	bool mark_as_interrupted = true;
	bool blocked = false;
	fpage_space_t* saved_space = NULL;
	uint8_t block_all_flag = 0;

	status = simple_ghmap_lookup_h(&private_data->signal_handler_table, signal->signal, false, 0, NULL, (void*)&handler, NULL);
	if (status != ferr_ok) {
		// this means that the given signal is not configured
		goto out;
	}

	if ((handler->configuration.flags & fsyscall_signal_configuration_flag_enabled) == 0) {
		// this signal is not enabled
		if ((handler->configuration.flags & fsyscall_signal_configuration_flag_kill_if_unhandled) != 0) {
			// we couldn't handle it, but if no one else can handle it, the target should be killed.
			status = ferr_aborted;
		} else {
			status = ferr_no_such_resource;
		}
		goto out;
	}

	if (uthread != target_uthread && (handler->configuration.flags & fsyscall_signal_configuration_flag_allow_redirection) == 0) {
		// this signal handler does not accept redirected signals from other uthreads
		status = ferr_no_such_resource;
		goto out;
	}

	saved_space = fpage_space_current();
	status = fpage_space_swap(data->user_space);
	if (status != ferr_ok) {
		goto out;
	}

	// FIXME: we shouldn't access the flag directly; we should have some sort of wrapper function
	//        that can gracefully handle invalid addresses.
	if (private_data->signal_mapping.block_all_flag && ferro_uio_atomic_load_1_relaxed((uintptr_t)private_data->signal_mapping.block_all_flag, &block_all_flag) == ferr_ok && block_all_flag != 0) {
		blocked = true;
	}

	// if either the handling thread OR the target thread are blocking the signal, consider it blocked.
	// the reasoning for this is that:
	// 1) obviously, if the handling thread is blocking signals, it doesn't want to be
	//    interrupted by any signal handlers (e.g. maybe it's modifying some data
	//    that a signal handler would need to use).
	// 2) if the target thread is blocking signals, it likely means the same thing:
	//    it's likely doing something that would cause issues with signal handlers,
	//    so if we have to suspend it (e.g. to handle a page fault), that would be a problem
	//    for the signal handler we want to run.
	if (uthread != target_uthread && target_private_data->signal_mapping.block_all_flag && ferro_uio_atomic_load_1_relaxed((uintptr_t)target_private_data->signal_mapping.block_all_flag, &block_all_flag) == ferr_ok && block_all_flag != 0) {
		blocked = true;
	}

	if (saved_space) {
		FERRO_WUR_IGNORE(fpage_space_swap(saved_space));
	}

	// however, the signal mask is only allowed to block *delivery* of signals to the given thread, in order to comply with POSIX.
	// (that's really the only reason it exists; for all other purposes, blocking all signals is preferable)
	if (signal->signal < 64 && (private_data->signal_mask & (1ull << signal->signal)) != 0) {
		blocked = true;
	}

	if (blocked && (flags & futhread_signal_flag_blockable) == 0) {
		status = ferr_should_restart;
		goto out;
	}

	simple_memcpy(&signal->configuration, &handler->configuration, sizeof(signal->configuration));

	if ((handler->configuration.flags & fsyscall_signal_configuration_flag_coalesce) != 0) {
		// this signal can be coalesed; try to see if we already have it queued
		status = ferr_no_such_resource;
		for (futhread_pending_signal_t* pending_signal = private_data->pending_signal; pending_signal != NULL; pending_signal = pending_signal->next) {
			if (pending_signal->signal == signal->signal && pending_signal->target_uthread == target_uthread) {
				status = ferr_ok;
				break;
			}
		}

		if (status == ferr_ok) {
			// we found it; no need to queue it
			if (signal) {
				FERRO_WUR_IGNORE(fmempool_free(signal));
			}
			goto out;
		}

		// otherwise, let's continue on to queue it up
		status = ferr_ok;
	}

	if (uthread != target_uthread && (handler->configuration.flags & fsyscall_signal_configuration_flag_block_on_redirect) != 0) {
		// we want to block the target uthread until the signal has been handled
		if (target_uthread == fthread_current()) {
			// obviously, we can't block ourselves until we fully queue up the signal
			// and drop the lock, so let's do that once we exit
			block_self = true;
			signal->was_blocked = true;
		} else {
			if ((flags & futhread_signal_flag_unblock_on_exit) == 0 && fthread_block(target_uthread, true) == ferr_ok) {
				// we blocked the thread, so we're responsible for unblocking it
				signal->was_blocked = true;
			}
		}
	}

	if ((handler->configuration.flags & fsyscall_signal_configuration_flag_preempt) == 0) {
		// this signal is not configured to preempt the thread;
		// just queue it onto the signal queue

		// no need to save the handling thread's user context in this case;
		// the context will only be saved once we actually try to handle it.

		// add it to the end of pending signal queue

		if (private_data->last_pending_signal) {
			signal->prev = &private_data->last_pending_signal->next;
		} else {
			signal->prev = &private_data->pending_signal;
		}
		signal->next = NULL;

		*signal->prev = signal;
		private_data->last_pending_signal = signal;
	} else {
		// this signal needs to preempt the handling thread

		// set it as the current signal

		signal->prev = &private_data->current_signal;
		signal->next = private_data->current_signal;

		*signal->prev = signal;
		if (signal->next) {
			signal->next->prev = &signal->next;
		}

		if (blocked) {
			// we're blocking this signal; don't actually preempt the thread.
			// FIXME: we need to mark the thread as needing preemption so that as soon as the block-all flag is cleared,
			//        the thread will be preempted. this can be done by checking in our "ending interrupt" handler
			//        whether the flag has been cleared and preempting the thread if so.
			mark_as_interrupted = false;
			goto out;
		}

		if (uthread == fthread_current()) {
			// we already know that we're in the kernel, so we know that we can let the syscall post-handler handle it.
		} else {
			FERRO_WUR_IGNORE(fthread_block(uthread, true));

			if (fthread_saved_context_is_kernel_space(uthread->saved_context)) {
				// the thread must be in a syscall
				// (which means we can let the post-handler handle it)
			} else {
				// the thread is executing in userspace
				// so we need to load it in ourselves right now.

				// unlink this signal from the current signal list
				*signal->prev = signal->next;
				if (signal->next) {
					signal->next->prev = signal->prev;
				}

				// set up the context to load in the signal handler
				saved_space = fpage_space_current();
				status = fpage_space_swap(data->user_space);
				if (status != ferr_ok) {
					goto out;
				}
				futhread_signal_setup_context(&private_data->signal_stack, signal, uthread->saved_context, uthread->saved_context, &private_data->signal_mask);
				if (saved_space) {
					FERRO_WUR_IGNORE(fpage_space_swap(saved_space));
				}

				// we can free the pending signal info now
				FERRO_WUR_IGNORE(fmempool_free(signal));
				signal = NULL;
			}

			FERRO_WUR_IGNORE(fthread_unblock(uthread));
		}
	}

out:
	flock_mutex_unlock(&private_data->signals_mutex);
out_unlocked:
	if (status == ferr_ok) {
		if (mark_as_interrupted) {
			// mark the target thread as interrupted and resume it so that
			// if it was waiting for something interruptibly in kernel-space,
			// it can wake up and see it has a signal pending
			fthread_mark_interrupted(uthread);
			FERRO_WUR_IGNORE(fthread_resume(uthread));
		}

		// FIXME: we are racing with the signal being handled before we block ourselves.
		//        we can fix this by instead having a waitq that we wait on while someone
		//        else handles our signal and then have them wake us up when they're done.
		if (block_self) {
			FERRO_WUR_IGNORE(fthread_block(fthread_current(), false));
		}
	}
	return status;
};

// FIXME: we need to handle the case when the handling thread is suspended
//        while another thread handles its redirected signal, but then a signal
//        arrives for the handling thread that is set as a preempting signal.
//        in this case, we need to simply queue up the preempting signal at the head of the
//        signal queue and have the thread handle it once it gets resumed.
ferr_t futhread_signal(fthread_t* uthread, uint64_t signal_number, fthread_t* target_uthread, futhread_signal_flags_t flags) {
	futhread_data_t* data = futhread_data_for_thread(uthread);
	futhread_data_private_t* private_data = (void*)data;
	futhread_data_t* target_data = futhread_data_for_thread(target_uthread);
	futhread_data_private_t* target_private_data = (void*)target_data;
	ferr_t status = ferr_ok;
	futhread_pending_signal_t* signal = NULL;

	if (!data || !target_data) {
		status = ferr_invalid_argument;
		goto out;
	}

	if (signal_number == 0) {
		status = ferr_invalid_argument;
		goto out;
	}

	status = fmempool_allocate(sizeof(*signal), NULL, (void*)&signal);
	if (status != ferr_ok) {
		goto out;
	}

	signal->prev = NULL;
	signal->next = NULL;
	signal->target_uthread = target_uthread;
	signal->signal = signal_number;
	signal->was_blocked = (flags & futhread_signal_flag_unblock_on_exit) != 0; // adjusted later
	signal->exited = false;
	signal->can_block = (flags & futhread_signal_flag_blockable) != 0;

	flock_mutex_lock(&private_data->signals_mutex);

	status = futhread_signal_internal(uthread, signal, target_uthread, flags);

out:
	if (status != ferr_ok) {
		if (signal) {
			FERRO_WUR_IGNORE(fmempool_free(signal));
		}
	}
	return status;
};

ferr_t futhread_signal_special(fthread_t* uthread, futhread_special_signal_t special_signal, fthread_t* target_uthread, futhread_signal_flags_t flags) {
	futhread_data_t* data = futhread_data_for_thread(uthread);
	futhread_data_private_t* private_data = (void*)data;
	futhread_data_t* target_data = futhread_data_for_thread(target_uthread);
	futhread_data_private_t* target_private_data = (void*)target_data;
	ferr_t status = ferr_ok;
	futhread_pending_signal_t* signal = NULL;

	if (!data || !target_data) {
		status = ferr_invalid_argument;
		goto out;
	}

	status = fmempool_allocate(sizeof(*signal), NULL, (void*)&signal);
	if (status != ferr_ok) {
		goto out;
	}

	signal->prev = NULL;
	signal->next = NULL;
	signal->target_uthread = target_uthread;
	//signal->signal = signal_number;
	signal->was_blocked = (flags & futhread_signal_flag_unblock_on_exit) != 0; // adjusted later
	signal->exited = false;
	signal->can_block = (flags & futhread_signal_flag_blockable) != 0;

	flock_mutex_lock(&private_data->signals_mutex);

	switch (special_signal) {
		case futhread_special_signal_bus_error:
			signal->signal = private_data->signal_mapping.bus_error_signal;
			break;
		case futhread_special_signal_page_fault:
			signal->signal = private_data->signal_mapping.page_fault_signal;
			break;
		case futhread_special_signal_floating_point_exception:
			signal->signal = private_data->signal_mapping.floating_point_exception_signal;
			break;
		case futhread_special_signal_illegal_instruction:
			signal->signal = private_data->signal_mapping.illegal_instruction_signal;
			break;
		case futhread_special_signal_debug:
			signal->signal = private_data->signal_mapping.debug_signal;
			break;
		case futhread_special_signal_division_by_zero:
			signal->signal = private_data->signal_mapping.division_by_zero_signal;
			break;

		default:
			status = ferr_invalid_argument;
			goto out;
	}

	if (signal->signal == 0) {
		flock_mutex_unlock(&private_data->signals_mutex);
		status = ferr_no_such_resource;
		goto out;
	}

	status = futhread_signal_internal(uthread, signal, target_uthread, flags);

out:
	if (status != ferr_ok) {
		if (signal) {
			FERRO_WUR_IGNORE(fmempool_free(signal));
		}
	}
	return status;
};

ferr_t futhread_handle_signals(fthread_t* uthread, bool locked) {
	ferr_t status = ferr_ok;
	futhread_data_t* data = futhread_data_for_thread(uthread);
	futhread_data_private_t* private_data = (void*)data;
	bool blocked = false;
	uint8_t block_all_flag = 0;

	if (!locked) {
		flock_mutex_lock(&private_data->signals_mutex);
	}

	if (private_data->signal_mapping.block_all_flag && ferro_uio_atomic_load_1_relaxed((uintptr_t)private_data->signal_mapping.block_all_flag, &block_all_flag) == ferr_ok && block_all_flag != 0) {
		blocked = true;
	}

retry:
	if (!private_data->current_signal) {
		// no current signal; let's check if there are any pending signals
		if (private_data->pending_signal) {
			futhread_pending_signal_t* signal = private_data->pending_signal;

			// unlink it from the pending signal queue
			*signal->prev = signal->next;
			if (signal->next) {
				signal->next->prev = signal->prev;
			}
			if (signal == private_data->last_pending_signal) {
				private_data->last_pending_signal = NULL;
			}

			// and link it into the current signal queue
			signal->prev = &private_data->current_signal;
			signal->next = private_data->current_signal;

			*signal->prev = signal;
			if (signal->next) {
				signal->next->prev = &signal->next;
			}
		}
	}

	if (private_data->current_signal) {
		futhread_data_private_t* target_private_data = (void*)futhread_data_for_thread(private_data->current_signal->target_uthread);
		if (target_private_data->signal_mapping.block_all_flag && ferro_uio_atomic_load_1_relaxed((uintptr_t)target_private_data->signal_mapping.block_all_flag, &block_all_flag) == ferr_ok && block_all_flag != 0) {
			blocked = true;
		}

		if (private_data->current_signal->signal < 64 && (private_data->signal_mask & (1ull << private_data->current_signal->signal)) != 0) {
			blocked = true;
		}
	}

	// if we're blocking signals and we have an unblockable signal that we want to load, we must kill the target uthread and its process (if it has one).
	if (blocked && private_data->current_signal && !private_data->current_signal->can_block) {
		futhread_pending_signal_t* signal = private_data->current_signal;
		fthread_t* target_thread = signal->target_uthread;

		// unlink this signal from the current signal list
		*signal->prev = signal->next;
		if (signal->next) {
			signal->next->prev = signal->prev;
		}

		if (signal->target_uthread == fthread_current() || futhread_process(signal->target_uthread) == fproc_current()) {
			// don't want to be holding the signal mutex when we die
			flock_mutex_unlock(&private_data->signals_mutex);

			// we also don't want to leak the signal's memory
			//
			// normally, this would be freed upon thread death,
			// but since we already unlinked it, it's not in the list, so it can't be freed.
			FERRO_WUR_IGNORE(fmempool_free(signal));
			signal = NULL;
		}

		if (futhread_process(target_thread)) {
			fproc_kill(futhread_process(target_thread));
		} else {
			FERRO_WUR_IGNORE(fthread_kill(target_thread));
		}

		// if we got here, the target uthread was not the current thread or a member of the current process.

		FERRO_WUR_IGNORE(fmempool_free(signal));
		signal = NULL;

		goto retry;
	}

	// if we have a signal to load (and we're not blocking signals), load it
	if (!blocked && private_data->current_signal) {
		futhread_pending_signal_t* signal = private_data->current_signal;

		status = ferr_signaled;

		// unlink this signal from the current signal list
		*signal->prev = signal->next;
		if (signal->next) {
			signal->next->prev = signal->prev;
		}

		// set up the context to load in the signal handler
		futhread_signal_setup_context(&private_data->signal_stack, signal, data->saved_syscall_context, data->saved_syscall_context, &private_data->signal_mask);

		// we can free the pending signal info now
		FERRO_WUR_IGNORE(fmempool_free(signal));
		signal = NULL;
	}

out:
	if (!locked) {
		flock_mutex_unlock(&private_data->signals_mutex);
	}
	return status;
};
