/*
 * This file is part of Anillo OS
 * Copyright (C) 2022 Anillo OS Developers
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

#include <gen/ferro/userspace/syscall-handlers.h>
#include <ferro/core/threads.h>
#include <ferro/userspace/threads.private.h>
#include <libsimple/general.h>
#include <ferro/core/scheduler.h>
#include <ferro/userspace/processes.h>
#include <ferro/core/mempool.h>

ferr_t fsyscall_handler_thread_signal_configure(uint64_t thread_id, uint64_t signal_number, const fsyscall_signal_configuration_t* new_configuration, fsyscall_signal_configuration_t* out_old_configuration) {
	ferr_t status = ferr_ok;
	fthread_t* uthread = fsched_find(thread_id, true);

	if (!uthread) {
		status = ferr_no_such_resource;
		goto out_unlocked;
	}

	futhread_data_t* data = futhread_data_for_thread(uthread);
	futhread_data_private_t* private_data = (void*)data;
	bool created = false;
	futhread_signal_handler_t* handler = NULL;

	if (!data) {
		status = ferr_invalid_argument;
		goto out_unlocked;
	}

	// TODO: support restartable signals
	if (new_configuration && (new_configuration->flags & fsyscall_signal_configuration_flag_autorestart) != 0) {
		status = ferr_unsupported;
		goto out_unlocked;
	}

	flock_mutex_lock(&private_data->signals_mutex);

	status = simple_ghmap_lookup_h(&private_data->signal_handler_table, signal_number, !!new_configuration, sizeof(*handler), &created, (void*)&handler, NULL);
	if (status != ferr_ok) {
		if (!!new_configuration) {
			goto out;
		} else {
			status = ferr_ok;
			if (out_old_configuration) {
				simple_memset(out_old_configuration, 0, sizeof(*out_old_configuration));
			}
			goto out;
		}
	}

	if (out_old_configuration) {
		if (created) {
			simple_memset(out_old_configuration, 0, sizeof(*out_old_configuration));
		} else {
			simple_memcpy(out_old_configuration, &handler->configuration, sizeof(*out_old_configuration));
		}
	}

	if (new_configuration) {
		handler->signal = signal_number;
		simple_memcpy(&handler->configuration, new_configuration, sizeof(handler->configuration));
	}

out:
	flock_mutex_unlock(&private_data->signals_mutex);
out_unlocked:
	return status;
};

#if FERRO_ARCH == FERRO_ARCH_x86_64
	#define FTHREAD_EXTRA_SAVE_SIZE FARCH_PER_CPU(xsave_area_size)
#elif FERRO_ARCH == FERRO_ARCH_aarch64
	#define FTHREAD_EXTRA_SAVE_SIZE 0
#endif

ferr_t fsyscall_handler_thread_signal_return(void) {
	ferr_t status = ferr_no_such_resource;
	fthread_t* uthread = fthread_current();
	futhread_data_t* data = futhread_data_for_thread(uthread);
	futhread_data_private_t* private_data = (void*)data;

	flock_mutex_lock(&private_data->signals_mutex);

	//
	// a signal may have come in while the signal mutex was dropped,
	// so the top-most current signal may be a different signal
	// than the one that we were called to exit from. however, any signal
	// that came in while we're in kernel-space cannot have loaded yet
	// (since it's only loaded when we exit a syscall or when it comes
	// in while we're in user-space). therefore, we just need to find
	// the top-most loaded signal, since that must be the one that called
	// us to exit.
	//

	// mark the top-most loaded signal as exited
	for (futhread_pending_signal_t* signal = private_data->current_signal; signal != NULL; signal = signal->next) {
		if (!signal->loaded) {
			continue;
		}

		// we found a signal to exit, so we can mark this syscall as succeeded.
		status = ferr_ok;

		signal->exited = true;
		break;
	}

	// unload all exited signals consecutively starting from the top
	//
	// the only one who's context really matters is the last one
	futhread_pending_signal_t* next = NULL;
	for (futhread_pending_signal_t* signal = private_data->current_signal; signal != NULL; signal = next) {
		next = signal->next;

		if (!signal->exited) {
			break;
		}

		// we found a signal to exit, so we can mark this syscall as succeeded.
		// this also prevents the syscall invoker from modifying the saved user context once we return.
		status = ferr_ok;

		// unlink this signal from the current signal list
		*signal->prev = signal->next;
		if (signal->next) {
			signal->next->prev = signal->prev;
		}

		simple_memcpy(data->saved_syscall_context, signal->saved_context, sizeof(*data->saved_syscall_context) + FTHREAD_EXTRA_SAVE_SIZE);

		// if the signal preempted userspace, we need to use a fake interrupt return to restore the entire userspace context
		// (without clobbering any registers like we do in a normal syscall return)
		private_data->use_fake_interrupt_return = signal->preempted;

		if (signal->was_blocked) {
			// we're responsible for unblocking the target uthread
			FERRO_WUR_IGNORE(fthread_unblock(signal->target_uthread));
		}

		// free this signal
		FERRO_WUR_IGNORE(fmempool_free(signal));
	}

out:
	flock_mutex_unlock(&private_data->signals_mutex);
out_unlocked:
	return status;
};

FERRO_STRUCT(thread_signal_iterator_context) {
	fthread_t* target_uthread;
	uint64_t signal_number;
};

static bool thread_signal_iterator(void* _context, fproc_t* process, fthread_t* uthread) {
	thread_signal_iterator_context_t* context = _context;

	if (uthread == context->target_uthread) {
		// skip this uthread
		return true;
	}

	if (futhread_signal(uthread, context->signal_number, context->target_uthread, false, true) == ferr_ok) {
		return false;
	}

	return true;
};

ferr_t fsyscall_handler_thread_signal(uint64_t target_thread_id, uint64_t signal_number) {
	ferr_t status = ferr_ok;
	fthread_t* uthread = fsched_find(target_thread_id, true);

	if (!uthread) {
		status = ferr_no_such_resource;
		goto out;
	}

	status = futhread_signal(uthread, signal_number, uthread, false, true);

	if (status == ferr_no_such_resource) {
		// try one of the other threads in its process (if it has one)
		fproc_t* process = futhread_process(uthread);

		if (process) {
			thread_signal_iterator_context_t context = {
				.target_uthread = uthread,
				.signal_number = signal_number,
			};

			if (fproc_for_each_thread(process, thread_signal_iterator, &context) != ferr_cancelled) {
				status = ferr_no_such_resource;
				goto out;
			}
		}
	}

out:
	return status;
};

ferr_t fsyscall_handler_thread_signal_update_mapping(uint64_t thread_id, fsyscall_signal_mapping_t const* new_mapping, fsyscall_signal_mapping_t* out_old_mapping) {
	ferr_t status = ferr_ok;
	fthread_t* uthread = fsched_find(thread_id, true);

	if (!uthread) {
		status = ferr_no_such_resource;
		goto out_unlocked;
	}


	futhread_data_t* data = futhread_data_for_thread(uthread);
	futhread_data_private_t* private_data = (void*)data;

	flock_mutex_lock(&private_data->signals_mutex);

	// FIXME: we should not access userspace memory directly
	//        (this includes reading from the flag later on)

	if (out_old_mapping) {
		simple_memcpy(out_old_mapping, &private_data->signal_mapping, sizeof(*out_old_mapping));
	}

	if (new_mapping) {
		simple_memcpy(&private_data->signal_mapping, new_mapping, sizeof(private_data->signal_mapping));
	}

out:
	flock_mutex_unlock(&private_data->signals_mutex);
out_unlocked:
	return status;
};
