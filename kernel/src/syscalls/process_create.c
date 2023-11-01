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
#include <ferro/userspace/processes.h>
#include <ferro/userspace/process-registry.h>
#include <libsimple/libsimple.h>
#include <ferro/userspace/threads.private.h>
#include <ferro/userspace/uio.h>

const fproc_descriptor_class_t fsyscall_proc_class = {
	.retain = (void*)fproc_retain,
	.release = (void*)fproc_release,
};

ferr_t fsyscall_handler_process_create(const fsyscall_process_create_info_t* user_info, uint64_t* out_process_handle) {
	ferr_t status = ferr_ok;
	fproc_t* proc = NULL;
	fproc_did_t process_handle = FPROC_DID_MAX;
	fpage_mapping_t* temp_mapping = NULL;
	size_t page_count = 0;
	fsyscall_process_create_info_t info;
	bool copied_regions = false;
	bool copied_descriptors = false;
	ferro_thread_context_t thread_context;
	bool copied_fp = false;
	futhread_data_t* data = NULL;

	status = ferro_uio_copy_in_noalloc((uintptr_t)user_info, sizeof(*user_info), &info);
	if (status != ferr_ok) {
		goto out;
	}

	status = ferro_uio_copy_in_noalloc((uintptr_t)info.thread_context, sizeof(*info.thread_context), &thread_context);
	if (status != ferr_ok) {
		goto out;
	}

	info.thread_context = &thread_context;

#if FERRO_ARCH == FERRO_ARCH_x86_64
	if (info.thread_context->xsave_area) {
		status = ferro_uio_copy_in((uintptr_t)info.thread_context->xsave_area, FARCH_PER_CPU(xsave_area_size), (void*)&info.thread_context->xsave_area);
		if (status != ferr_ok) {
			goto out;
		}

		copied_fp = true;
	}
#elif FERRO_ARCH == FERRO_ARCH_aarch64
	if (info.thread_context->fp_registers) {
		status = ferro_uio_copy_in((uintptr_t)info.thread_context->fp_registers, sizeof(data->saved_syscall_context->fp_registers), (void*)&info.thread_context->fp_registers);
		if (status != ferr_ok) {
			goto out;
		}

		copied_fp = true;
	}
#endif

	if (info.region_count > 0) {
		status = ferro_uio_copy_in((uintptr_t)info.regions, sizeof(*info.regions) * info.region_count, (void*)&info.regions);
		if (status != ferr_ok) {
			goto out;
		}

		copied_regions = true;
	}

	if (info.descriptor_count > 0) {
		status = ferro_uio_copy_in((uintptr_t)info.descriptors, sizeof(*info.descriptors) * info.descriptor_count, (void*)&info.descriptors);
		if (status != ferr_ok) {
			goto out;
		}

		copied_descriptors = true;
	}

	status = fproc_new(NULL, fproc_current(), &proc);
	if (status != ferr_ok) {
		goto out;
	}

	status = fprocreg_register(proc);
	if (status != ferr_ok) {
		status = ferr_temporary_outage;
		fproc_kill(proc);
		goto out;
	}

	//
	// install descriptors
	//

	for (size_t i = 0; i < info.descriptor_count; ++i) {
		void* desc = NULL;
		const fproc_descriptor_class_t* desc_class = NULL;
		fproc_did_t installed_did = FPROC_DID_MAX;

		status = fproc_lookup_descriptor(fproc_current(), info.descriptors[i], false, &desc, &desc_class);
		if (status != ferr_ok) {
			goto out;
		}

		status = fproc_install_descriptor(proc, desc, desc_class, &installed_did);
		if (status != ferr_ok) {
			goto out;
		}

		// this should never happen, but just in case...
		if (installed_did != i) {
			status = ferr_unknown;
			goto out;
		}
	}

	//
	// copy memory
	//

	for (size_t i = 0; i < info.region_count; ++i) {
		page_count += fpage_round_up_to_page_count(info.regions[i].source.length);
	}

	status = fpage_mapping_new(page_count, 0, &temp_mapping);
	if (status != ferr_ok) {
		goto out;
	}

	size_t page_offset = 0;
	for (size_t i = 0; i < info.region_count; ++i) {
		void* tmp = NULL;
		size_t page_count = fpage_round_up_to_page_count(info.regions[i].source.length);

		status = fpage_space_insert_mapping(fpage_space_current(), temp_mapping, page_offset, page_count, 0, NULL, 0, &tmp);
		if (status != ferr_ok) {
			goto out;
		}

		status = ferro_uio_copy_in_noalloc((uintptr_t)info.regions[i].source.start, info.regions[i].source.length, tmp);
		if (status != ferr_ok) {
			goto out;
		}

		fpanic_status(fpage_space_remove_mapping(fpage_space_current(), tmp));

		status = fpage_space_insert_mapping(&proc->space, temp_mapping, page_offset, page_count, 0, info.regions[i].destination, fpage_flag_zero | fpage_flag_unprivileged, NULL);
		if (status != ferr_ok) {
			goto out;
		}

		status = fproc_register_mapping(proc, info.regions[i].destination, page_count, 0, temp_mapping);
		if (status != ferr_ok) {
			goto out;
		}

		page_offset += page_count;
	}

	//
	// set up the context
	//

	data = &proc->uthread_list->public;

#if FERRO_ARCH == FERRO_ARCH_x86_64
	data->saved_syscall_context->cs = (farch_int_gdt_index_code_user * 8) | 3;
	data->saved_syscall_context->ss = (farch_int_gdt_index_data_user * 8) | 3;

	data->saved_syscall_context->rax = info.thread_context->rax;
	data->saved_syscall_context->rcx = info.thread_context->rcx;
	data->saved_syscall_context->rdx = info.thread_context->rdx;
	data->saved_syscall_context->rbx = info.thread_context->rbx;
	data->saved_syscall_context->rsi = info.thread_context->rsi;
	data->saved_syscall_context->rdi = info.thread_context->rdi;
	data->saved_syscall_context->rsp = info.thread_context->rsp;
	data->saved_syscall_context->rbp = info.thread_context->rbp;
	data->saved_syscall_context->r8 = info.thread_context->r8;
	data->saved_syscall_context->r9 = info.thread_context->r9;
	data->saved_syscall_context->r10 = info.thread_context->r10;
	data->saved_syscall_context->r11 = info.thread_context->r11;
	data->saved_syscall_context->r12 = info.thread_context->r12;
	data->saved_syscall_context->r13 = info.thread_context->r13;
	data->saved_syscall_context->r14 = info.thread_context->r14;
	data->saved_syscall_context->r15 = info.thread_context->r15;
	data->saved_syscall_context->rip = info.thread_context->rip;

	// only allow userspace to modify the following CPU flags:
	//   * carry (bit 0)
	//   * parity (bit 2)
	//   * adjust (bit 4)
	//   * zero (bit 6)
	//   * sign (bit 7)
	//   * direction (bit 10)
	//   * overflow (bit 11)
	//
	// additionally, we always OR in the following flags:
	//   * always-one (bit 1)
	//   * interrupt-enable (bit 9)
	data->saved_syscall_context->rflags = (info.thread_context->rflags & 0xcd5) | 0x202;

	if (info.thread_context->xsave_area) {
		// now copy the xsave area
		// TODO: verify that the xsave area is valid; this just means we need to validate the xsave header
		simple_memcpy(data->saved_syscall_context->xsave_area, info.thread_context->xsave_area, FARCH_PER_CPU(xsave_area_size));
	}
#elif FERRO_ARCH == FERRO_ARCH_aarch64
	data->saved_syscall_context->x0 = info.thread_context->x0;
	data->saved_syscall_context->x1 = info.thread_context->x1;
	data->saved_syscall_context->x2 = info.thread_context->x2;
	data->saved_syscall_context->x3 = info.thread_context->x3;
	data->saved_syscall_context->x4 = info.thread_context->x4;
	data->saved_syscall_context->x5 = info.thread_context->x5;
	data->saved_syscall_context->x6 = info.thread_context->x6;
	data->saved_syscall_context->x7 = info.thread_context->x7;
	data->saved_syscall_context->x8 = info.thread_context->x8;
	data->saved_syscall_context->x9 = info.thread_context->x9;
	data->saved_syscall_context->x10 = info.thread_context->x10;
	data->saved_syscall_context->x11 = info.thread_context->x11;
	data->saved_syscall_context->x12 = info.thread_context->x12;
	data->saved_syscall_context->x13 = info.thread_context->x13;
	data->saved_syscall_context->x14 = info.thread_context->x14;
	data->saved_syscall_context->x15 = info.thread_context->x15;
	data->saved_syscall_context->x16 = info.thread_context->x16;
	data->saved_syscall_context->x17 = info.thread_context->x17;
	data->saved_syscall_context->x18 = info.thread_context->x18;
	data->saved_syscall_context->x19 = info.thread_context->x19;
	data->saved_syscall_context->x20 = info.thread_context->x20;
	data->saved_syscall_context->x21 = info.thread_context->x21;
	data->saved_syscall_context->x22 = info.thread_context->x22;
	data->saved_syscall_context->x23 = info.thread_context->x23;
	data->saved_syscall_context->x24 = info.thread_context->x24;
	data->saved_syscall_context->x25 = info.thread_context->x25;
	data->saved_syscall_context->x26 = info.thread_context->x26;
	data->saved_syscall_context->x27 = info.thread_context->x27;
	data->saved_syscall_context->x28 = info.thread_context->x28;
	data->saved_syscall_context->x29 = info.thread_context->x29;
	data->saved_syscall_context->x30 = info.thread_context->x30;
	data->saved_syscall_context->pc = info.thread_context->pc;
	data->saved_syscall_context->sp = info.thread_context->sp;

	data->saved_syscall_context->fpsr = info.thread_context->fpsr;
	data->saved_syscall_context->fpcr = info.thread_context->fpcr;

	// only allow userspace to modify the following CPU flags:
	//   * negative (bit 31)
	//   * zero (bit 30)
	//   * carry (bit 29)
	//   * overflow (bit 28)
	data->saved_syscall_context->pstate = (info.thread_context->pstate & 0xf0000000ull) | farch_thread_pstate_aarch64 | farch_thread_pstate_el0 | farch_thread_pstate_sp0;

	if (info.thread_context->fp_registers) {
		// now copy the FP registers
		simple_memcpy(data->saved_syscall_context->fp_registers, info.thread_context->fp_registers, sizeof(data->saved_syscall_context->fp_registers));
	}
#endif

	if (info.flags & fsyscall_process_create_flag_use_default_stack) {
#if FERRO_ARCH == FERRO_ARCH_x86_64
		data->saved_syscall_context->rsp = (uintptr_t)data->user_stack_base + data->user_stack_size;
#elif FERRO_ARCH == FERRO_ARCH_aarch64
		data->saved_syscall_context->sp = (uintptr_t)data->user_stack_base + data->user_stack_size;
#endif
	}

	//
	// install the handle in the current process
	//

	status = fproc_install_descriptor(fproc_current(), proc, &fsyscall_proc_class, &process_handle);
	if (status != ferr_ok) {
		goto out;
	}

	// now remove descriptors from the original process
	// (they're transferred from the parent to the child upon success)
	for (size_t i = 0; i < info.descriptor_count; ++i) {
		void* desc = NULL;
		const fproc_descriptor_class_t* desc_class = NULL;
		fproc_did_t installed_did = FPROC_DID_MAX;

		status = fproc_lookup_descriptor(fproc_current(), info.descriptors[i], false, &desc, &desc_class);
		if (status != ferr_ok) {
			// race?
			continue;
		}

		// this cannot fail
		FERRO_WUR_IGNORE(fproc_uninstall_descriptor(proc, installed_did));
	}

	if (out_process_handle) {
		status = ferro_uio_copy_out(&process_handle, sizeof(process_handle), (uintptr_t)out_process_handle);
	}

out:
	if (status != ferr_ok) {
		if (process_handle) {
			FERRO_WUR_IGNORE(fproc_uninstall_descriptor(fproc_current(), process_handle));
		}
		if (proc) {
			fproc_kill(proc);
		}
	}

	if (proc) {
		fproc_release(proc);
	}

	if (temp_mapping) {
		fpage_mapping_release(temp_mapping);
	}

	if (copied_descriptors) {
		ferro_uio_copy_free((void*)info.descriptors, sizeof(*info.descriptors) * info.descriptor_count);
	}

	if (copied_regions) {
		ferro_uio_copy_free((void*)info.regions, sizeof(*info.regions) * info.region_count);
	}

#if FERRO_ARCH == FERRO_ARCH_x86_64
	if (copied_fp) {
		ferro_uio_copy_free((void*)info.thread_context->xsave_area, FARCH_PER_CPU(xsave_area_size));
	}
#elif FERRO_ARCH == FERRO_ARCH_aarch64
	if (copied_fp) {
		ferro_uio_copy_free((void*)info.thread_context->fp_registers, sizeof(data->saved_syscall_context->fp_registers));
	}
#endif

	return status;
};
