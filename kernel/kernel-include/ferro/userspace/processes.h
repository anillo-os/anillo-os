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

#ifndef _FERRO_USERSPACE_PROCESSES_H_
#define _FERRO_USERSPACE_PROCESSES_H_

#include <ferro/base.h>
#include <ferro/error.h>
#include <ferro/core/vfs.h>
#include <ferro/core/threads.h>
#include <ferro/core/paging.h>
#include <ferro/userspace/loader.h>
#include <ferro/core/ghmap.h>
#include <ferro/core/paging.private.h>
#include <ferro/core/refcount.h>
#include <ferro/userspace/futex.h>

FERRO_DECLARATIONS_BEGIN;

typedef uint64_t fproc_fd_t;
#define FPROC_FD_MAX UINT64_MAX

FERRO_STRUCT_FWD(futhread_data_private);

/**
 * Processes are purely a userspace concept (thus, no `u` prefix).
 * They are a way of distinguishing groups of threads cooperating for the same purpose, sharing resources like memory.
 *
 * Basically, the difference between a process and a thread is that the purpose of a process is to achieve a major goal (e.g. print something on the screen, manage a device,
 * modify some files, etc.) while the purpose of a thread is to execute code (e.g. perform some calculation for the printing, wait for the device to become active,
 * request access to the files, etc.). One could also say that the purpose of a thread is to achieve a minor goal; one that contributes towards the completion of the process' goal.
 *
 * Also note that all processes must have threads, but not necessarily vice versa.
 * It is possible to create a kernel-space or even userspace thread without a process (although a userspace thread without a process is not very useful).
 *
 * A process has no execution state of its own. Instead, each of its threads has its own execution state and they can be suspended, resumed, and killed individually.
 * When a process has no more threads left alive, it is considered dead. However, the information structure will not be released until the last reference to it is released.
 * That way, you can inspect the final state of the process when it died and perform certain cleanup, if necessary.
 */
FERRO_STRUCT(fproc) {
	/**
	 * Number of references held on this process. If this drops to `0`, the process is released.
	 *
	 * This MUST be accessed and modified ONLY with fproc_retain() and fproc_release().
	 */
	frefcount_t reference_count;

	/**
	 * The user address space for this process.
	 * It is shared among the threads in the process.
	 */
	fpage_space_t space;

	/**
	 * The list of uthreads in this process.
	 */
	futhread_data_private_t* uthread_list;
	flock_mutex_t uthread_list_mutex;

	fuloader_info_t* binary_info;

	/**
	 * A VFS file descriptor pointing to the main binary for this process.
	 *
	 * As long as the process is alive, it holds a live descriptor pointing to its binary.
	 */
	fvfs_descriptor_t* binary_descriptor;

	/**
	 * A linked list of all the mappings in the process.
	 */
	fpage_mapping_t* mappings;

	/**
	 * A mutex that protects #mappings.
	 */
	flock_mutex_t mappings_mutex;

	/**
	 * A table of all the file descriptors currently held by this process.
	 */
	simple_ghmap_t descriptor_table;

	/**
	 * The lowest of the next available FD numbers.
	 */
	fproc_fd_t next_lowest_fd;

	/**
	 * The highest FD number currently in-use.
	 *
	 * If both this and #next_lowest_fd are `0`, no FDs are currently in-use.
	 */
	fproc_fd_t highest_fd;

	/**
	 * A mutex that protects #descriptor_table, #next_lowest_fd, and #highest_fd.
	 */
	flock_mutex_t descriptor_table_mutex;

	/**
	 * Waiters here are notified right before process resources are released, so any leaked file descriptors and memory
	 * are still available when waiters are notified.
	 */
	fwaitq_t death_wait;

	/**
	 * Waiters here are notified right before the process structure is released, so the pointer is still valid.
	 * However, by this point, most of the resources have already been released.
	 */
	fwaitq_t destroy_wait;

	simple_ghmap_t per_proc;
	flock_mutex_t per_proc_mutex;

	futex_table_t futex_table;
};

/**
 * Creates a new process for the binary pointed to by the given file descriptor.
 *
 * A process's initial thread is suspended upon creation; it must be resumed (with fthread_resume()) for execution to start.
 *
 * @param file_descriptor A file descriptor pointing to the binary for the new process to execute.
 * @param out_proc        A pointer into which a pointer to the new process's information structure will be written.
 *
 * @note This function grants the caller a single reference on the newly created process.
 *
 * @retval ferr_ok               The process has been successfully created and @p out_proc now contains a pointer to its information structure.
 * @retval ferr_temporary_outage There were not enough resources to create the new process.
 * @retval ferr_invalid_argument One or more of: 1) @p file_descriptor was not a valid VFS file descriptor, 2) @p out_proc was `NULL`, 3) @p file_descriptor did not point to a valid binary (or the maybe the binary's interpreter had this issue).
 * @retval ferr_no_such_resource Can only occur when the binary has an interpreter and the binary's interpreter could not be found.
 * @retval ferr_forbidden        One or more of: 1) reading and executing the binary was not allowed, 2) reading and executing the binary's interpreter was not allowed.
 */
FERRO_WUR ferr_t fproc_new(fvfs_descriptor_t* file_descriptor, fproc_t** out_proc);

/**
 * Retrieves a pointer to the process information structure for the process that is currently executing on the current CPU.
 *
 * The returned pointer MAY be `NULL` if there is no active process on the current CPU.
 *
 * However, in an interrupt context, this will return the process that was executing when the interrupt occurred.
 *
 * @note This function DOES NOT grant a reference on the process. However, because this returns the *current* process, callers can rest assured that the process *is* valid.
 */
fproc_t* fproc_current(void);

/**
 * Tries to retain the given process.
 *
 * @param process The process to try to retain.
 *
 * Return values:
 * @retval ferr_ok               The process was successfully retained.
 * @retval ferr_permanent_outage The process was deallocated while this call occurred. It is no longer valid.
 */
FERRO_WUR ferr_t fproc_retain(fproc_t* process);

/**
 * Releases the given process.
 *
 * @param process The process to release.
 */
void fproc_release(fproc_t* process);

/**
 * Installs a new FD in the given process, associating it with the given descriptor.
 *
 * @param process    The process to install the FD in.
 * @param descriptor The descriptor that should be associated with the new FD.
 *                   This descriptor will be retained by this operation and released once the FD is closed.
 * @param out_fd     A pointer in which to write the FD number of the new FD.
 *
 * @retval ferr_ok               A new FD has been created and @p descriptor has been retain and associated with it. The new FD's number has been written into @p out_fd.
 * @retval ferr_invalid_argument One or more of: 1) @p process was not a valid process, 2) @p descriptor was not a valid descriptor, 3) @p out_fd was `NULL`.
 * @retval ferr_temporary_outage There were insufficient resources to allocate a new FD within the given process.
 * @retval ferr_forbidden        Installing a new FD for the given descriptor in the given process was not allowed.
 */
ferr_t fproc_install_descriptor(fproc_t* process, fvfs_descriptor_t* descriptor, fproc_fd_t* out_fd);

/**
 * Uninstalls the given FD from the given processing, releasing the VFS descriptor associated with it.
 *
 * @param process The process to uninstall the FD from.
 * @param fd      The FD number for the FD that should be uninstalled.
 *
 * @retval ferr_ok               The FD was found and uninstalled from the given process; the descriptor associated with it has been released.
 * @retval ferr_invalid_argument One or more of: 1) @p process was not a valid process.
 * @retval ferr_no_such_resource No FD with the given FD number could be found in the given process.
 * @retval ferr_forbidden        Uninstalling the given FD from the given process was not allowed.
 */
ferr_t fproc_uninstall_descriptor(fproc_t* process, fproc_fd_t fd);

/**
 * Looks up (and optionally retains) the descriptor associated with the given FD in the given process.
 *
 * @param process        The process to lookup the FD in.
 * @param fd             The FD number of the FD associated with the given descriptor.
 * @param retain         Whether to retain the descriptor on success.
 * @param out_descriptor A conditionally optional pointer in which to write a pointer to the descriptor associated with the given FD.
 *
 * @note If the descriptor is retained before returning (i.e. when @p retain is `true`),
 *       retention happens atomically. In other words, if a lookup occurs simultaneously with
 *       a closure, either the FD will be closed first and the descriptor reference lost (and the lookup would fail)
 *       or the lookup will occur first and the descriptor retained to be returned (and thus, the validity of the
 *       returned descriptor would not be affect by the FD closure).
 *
 * @note If @p retain is `true`, @p out_descriptor CANNOT be `NULL`, so that it can be released by the caller later.
 *       If @p retain is `false`, @p out_descriptor is allowed to be `NULL`, so that can be used to check if a descriptor was valid at the time of the call.
 *       Note that, due to multithreading, the descriptor's validity may have already changed by the time the call returns.
 *
 * @retval ferr_ok               The FD was found and the associated descriptor has been returned.
 * @retval ferr_invalid_argument One or more of: 1) @p process was not a valid process, 2) @p retain was `true` but @p out_descriptor was `NULL`.
 * @retval ferr_no_such_resource No FD with the given FD number could be found in the given process.
 * @retval ferr_forbidden        Looking up the given FD in the given process was not allowed.
 */
ferr_t fproc_lookup_descriptor(fproc_t* process, fproc_fd_t fd, bool retain, fvfs_descriptor_t** out_descriptor);

/**
 * Registers the given region of memory in the process' memory mappings.
 *
 * @param process    The process to register the mapping in.
 * @param address    The starting address of the region to register.
 * @param page_count The number of pages in the region.
 *
 * @note This function DOES NOT allocate memory. All it does is save some information about the given memory region in the process' mappings list.
 *
 * @retval ferr_ok                  The mapping was successfully registered in the process.
 * @retval ferr_invalid_argument    One or more of: 1) @p process was not a valid process.
 * @retval ferr_temporary_outage    There were insufficient resources to register the mapping.
 * @retval ferr_forbidden           Registering the mapping in the given process was not allowed.
 * @retval ferr_already_in_progress Either part or all of the region was already mapped.
 */
ferr_t fproc_register_mapping(fproc_t* process, void* address, size_t page_count);

/**
 * Unregisters the mapping starting at the given address from the process' memory mappings.
 *
 * @param process        The process to unregister the mapping from.
 * @param address        The starting address of the region to unregister.
 * @param out_page_count An optional pointer in which to write the number of pages the mapping spans.
 *
 * @retval ferr_ok               The mapping was successfully unregistered from the process.
 * @retval ferr_invalid_argument One or more of: 1) @p process was not a valid process.
 * @retval ferr_no_such_resource No mapping starting at the given address was found in the process.
 * @retval ferr_forbidden        Unregistering the mapping in the given process was not allowed.
 */
ferr_t fproc_unregister_mapping(fproc_t* process, void* address, size_t* out_page_count);

/**
 * Looks up the mapping starting at the given address in the process' memory mappings and returns how many pages it occupies.
 *
 * @param process        The process to lookup the mapping in.
 * @param address        The starting address of the region to lookup.
 * @param out_page_count An optional pointer in which to write the number of pages the mapping spans.
 *
 * @retval ferr_ok               The mapping was found and (if @p out_page_count was not `NULL`) the number of pages it spans has been written into @p out_page_count.
 * @retval ferr_invalid_argument One or more of: 1) @p process was not a valid process.
 * @retval ferr_no_such_resource No mapping starting at the given address was found in the process.
 * @retval ferr_forbidden        Looking up the mapping in the given process was not allowed.
 */
ferr_t fproc_lookup_mapping(fproc_t* process, void* address, size_t* out_page_count);

typedef uint64_t fper_proc_key_t;
typedef void (*fper_proc_data_destructor_f)(void* context, void* data, size_t data_size);

FERRO_WUR ferr_t fper_proc_register(fper_proc_key_t* out_key);
FERRO_WUR ferr_t fper_proc_lookup(fproc_t* process, fper_proc_key_t key, bool create_if_absent, size_t size_if_absent, fper_proc_data_destructor_f destructor_if_absent, void* destructor_context, bool* out_created, void** out_pointer, size_t* out_size);
FERRO_WUR ferr_t fper_proc_clear(fproc_t* process, fper_proc_key_t key, bool skip_previous_destructor);

typedef bool (*fproc_for_each_thread_iterator_f)(void* context, fproc_t* process, fthread_t* thread);

/**
 * Calls the given iterator for each thread in the process.
 *
 * @param process  The process whose threads will be iterated through.
 * @param iterator The iterator function to call on each thread in the process.
 * @param context  An optional context to pass to the iterator.
 *
 * @retval ferr_ok        The iterator was successfully called on each thread in the process.
 * @retval ferr_cancelled The iterator exited the iteration early by returning `false`.
 */
ferr_t fproc_for_each_thread(fproc_t* process, fproc_for_each_thread_iterator_f iterator, void* context);

/**
 * Suspends the given process by suspending all its threads.
 *
 * @param process The process whose threads will be suspended.
 *
 * @retval ferr_ok        The threads in the process were successfully suspended.
 * @retval ferr_forbidden Suspending the threads in the given process was not allowed.
 */
ferr_t fproc_suspend(fproc_t* process);

/**
 * Resumes the given process by resuming all its threads.
 *
 * @param process The process whose threads will be resumed.
 *
 * @retval ferr_ok        The threads in the process were successfully resumed.
 * @retval ferr_forbidden Resuming the threads in the given process was not allowed.
 */
ferr_t fproc_resume(fproc_t* process);

/**
 * Attaches the given uthread to the given process.
 *
 * @param process The process who will now own the uthread.
 * @param uthread The uthread to attach to the process.
 *
 * @pre The thread has already been registered as a uthread but it has not been attached to any process yet.
 *
 * @retval ferr_ok               The uthread has been successfully attached to the process.
 * @retval ferr_permanent_outage The process, the uthread, or both could not be retained.
 */
ferr_t fproc_attach_thread(fproc_t* process, fthread_t* uthread);

FERRO_DECLARATIONS_END;

#endif // _FERRO_USERSPACE_PROCESSES_H_
