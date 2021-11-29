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

#include <dymple/relocations.h>
#include <dymple/log.h>
#include <dymple/leb128.h>
#include <dymple/resolution.private.h>
#include <dymple/api.private.h>

#include <libmacho/libmacho.h>

DYMPLE_STRUCT(dymple_bind_info) {
	macho_relocation_type_t relocation_type;
	size_t segment_index;
	size_t segment_offset;
	const char* symbol_name;
	size_t symbol_name_length;
	uintmax_t library_ordinal;
	intmax_t addend;
	uint8_t flags;
};

static ferr_t dymple_perform_rebase(dymple_image_t* image, macho_relocation_type_t relocation_type, size_t segment_index, size_t segment_offset) {
	ferr_t status = ferr_ok;
	void* address = (char*)image->segments[segment_index].address + segment_offset;

	dymple_log_debug(dymple_log_category_relocations, "Rebase %p (file load base = %p; segment = %zu -> %p; offset = %zu = %zx; type = %u)\n", address, image->file_load_base, segment_index, image->segments[segment_index].address, segment_offset, segment_offset, relocation_type);

	switch (relocation_type) {
		case macho_relocation_type_pointer:
		case macho_relocation_type_text_absolute_32:
			// rebase the address by adding the image's base address to it
			dymple_log_debug(dymple_log_category_relocations, "Rebase value from %p to %p\n", (void*)*(uintptr_t*)address, (void*)(*(uintptr_t*)address + (uintptr_t)image->base));
			*(uintptr_t*)address = *(uintptr_t*)address - (uintptr_t)image->file_load_base + (uintptr_t)image->base;
			break;

		// apparently, macho_relocation_type_text_pc_relative_32 is unsupported in dyld
		// so i guess we don't know how to handle it
		default:
			return ferr_invalid_argument;
	}

out:
	return status;
};

static ferr_t dymple_perform_bind(dymple_image_t* image, const dymple_bind_info_t* bind_info, void** out_bound_symbol_address) {
	ferr_t status = ferr_ok;
	dymple_image_t* library = NULL;
	dymple_symbol_t* symbol_to_bind = NULL;
	uintptr_t new_value = 0;
	void* address = (char*)image->segments[bind_info->segment_index].address + bind_info->segment_offset;

	// TODO: handle special library indicies
	if (bind_info->library_ordinal > 0 && bind_info->library_ordinal <= image->dependency_count) {
		library = image->dependencies[bind_info->library_ordinal - 1];
	} else {
		dymple_log_error(dymple_log_category_relocations, "Invalid library ordinal %ju\n", bind_info->library_ordinal);
		sys_abort();
	}

	dymple_log_debug(dymple_log_category_relocations, "Bind %p (file load base = %p; segment = %zu -> %p; offset = %zu = %zx; type = %u) to symbol %.*s from %.*s (flags = %u)\n", address, image->file_load_base, bind_info->segment_index, image->segments[bind_info->segment_index].address, bind_info->segment_offset, bind_info->segment_offset, bind_info->relocation_type, (int)bind_info->symbol_name_length, bind_info->symbol_name, (int)library->name_length, library->name, bind_info->flags);

	status = dymple_resolve_export(library, bind_info->symbol_name, bind_info->symbol_name_length, &symbol_to_bind);
	if (status != ferr_ok) {
		goto out;
	}

	dymple_log_debug(dymple_log_category_relocations, "Bind %p to %p (with addend = %ju)\n", address, dymple_symbol_address(symbol_to_bind), bind_info->addend);

	new_value = (uintptr_t)dymple_symbol_address(symbol_to_bind) + bind_info->addend;

	switch (bind_info->relocation_type) {
		case macho_relocation_type_pointer: {
			*(uintptr_t*)address = new_value;
		} break;

		case macho_relocation_type_text_absolute_32: {
			*(uint32_t*)address = (uint32_t)new_value;
		} break;

		case macho_relocation_type_text_pc_relative_32: {
			*(uint32_t*)address = (uint32_t)(new_value - ((uintptr_t)address + 4));
		} break;

		default: {
			dymple_log_error(dymple_log_category_relocations, "Unsupported relocation type for bind %u\n", bind_info->relocation_type);
			sys_abort();
		} break;
	}

	if (out_bound_symbol_address) {
		*out_bound_symbol_address = (void*)new_value;
	}

out:
	return status;
};

static ferr_t dymple_relocate_image_perform_rebase(dymple_image_t* image, dymple_relocation_info_t* info) {
	ferr_t status = ferr_ok;
	macho_relocation_type_t reloc_type = 0;
	uint8_t segment_index = 0;
	uintmax_t segment_offset = 0;

	size_t uleb_size = 0;

	dymple_log_debug(dymple_log_category_relocations, "Rebase instructions size: %zu\n", info->rebase_instructions_size);

	dymple_log_debug(dymple_log_category_relocations, "Rebase instructions:\n");
	for (size_t i = 0; i < info->rebase_instructions_size; ++i) {
		dymple_log_debug(dymple_log_category_relocations, "%x ", ((char*)info->rebase_instructions)[i]);
	}
	dymple_log_debug(dymple_log_category_relocations, "\n");

	for (size_t i = 0; i < info->rebase_instructions_size; /* handled in the body */) {
		macho_rebase_opcode_t opcode = macho_relocation_instruction_get_opcode(((char*)info->rebase_instructions)[i]);
		uint8_t immediate = macho_relocation_instruction_get_immediate(((char*)info->rebase_instructions)[i]);
		bool done = false;

		++i;

		switch (opcode) {
			case macho_rebase_opcode_done: {
				done = true;
			} break;

			case macho_rebase_opcode_set_type_immediate: {
				reloc_type = immediate;
			} break;

			case macho_rebase_opcode_set_segment_immediate_and_offset_uleb: {
				segment_index = immediate;

				status = dymple_leb128_decode_unsigned(&info->rebase_instructions[i], info->rebase_instructions_size - i, &segment_offset, &uleb_size);
				if (status != ferr_ok) {
					goto out;
				}
				i += uleb_size;
			} break;

			case macho_rebase_opcode_add_address_uleb: {
				uintmax_t tmp = 0;

				status = dymple_leb128_decode_unsigned(&info->rebase_instructions[i], info->rebase_instructions_size - i, &tmp, &uleb_size);
				if (status != ferr_ok) {
					goto out;
				}
				i += uleb_size;

				segment_offset += tmp;
			} break;

			case macho_rebase_opcode_add_immediate_scaled: {
				segment_offset += immediate * sizeof(void*);
			} break;

			case macho_rebase_opcode_perform_rebase_immediate_times: {
				for (uint8_t j = 0; j < immediate; ++j) {
					status = dymple_perform_rebase(image, reloc_type, segment_index, segment_offset);
					if (status != ferr_ok) {
						goto out;
					}

					segment_offset += sizeof(void*);
				}
			} break;

			case macho_rebase_opcode_perform_rebase_uleb_times: {
				uintmax_t times = 0;

				status = dymple_leb128_decode_unsigned(&info->rebase_instructions[i], info->rebase_instructions_size - i, &times, &uleb_size);
				if (status != ferr_ok) {
					goto out;
				}
				i += uleb_size;

				for (uintmax_t j = 0; j < times; ++j) {
					status = dymple_perform_rebase(image, reloc_type, segment_index, segment_offset);
					if (status != ferr_ok) {
						goto out;
					}

					segment_offset += sizeof(void*);
				}
			} break;

			case macho_rebase_opcode_perform_rebase_add_uleb: {
				uintmax_t offset = 0;

				status = dymple_leb128_decode_unsigned(&info->rebase_instructions[i], info->rebase_instructions_size - i, &offset, &uleb_size);
				if (status != ferr_ok) {
					goto out;
				}
				i += uleb_size;

				status = dymple_perform_rebase(image, reloc_type, segment_index, segment_offset);
				if (status != ferr_ok) {
					goto out;
				}

				segment_offset += sizeof(void*) + offset;
			} break;

			case macho_rebase_opcode_perform_rebase_uleb_times_skipping_uleb: {
				uintmax_t times = 0;
				uintmax_t skip = 0;

				status = dymple_leb128_decode_unsigned(&info->rebase_instructions[i], info->rebase_instructions_size - i, &times, &uleb_size);
				if (status != ferr_ok) {
					goto out;
				}
				i += uleb_size;

				status = dymple_leb128_decode_unsigned(&info->rebase_instructions[i], info->rebase_instructions_size - i, &skip, &uleb_size);
				if (status != ferr_ok) {
					goto out;
				}
				i += uleb_size;

				for (uintmax_t j = 0; j < times; ++j) {
					status = dymple_perform_rebase(image, reloc_type, segment_index, segment_offset);
					if (status != ferr_ok) {
						goto out;
					}

					segment_offset += sizeof(void*) + skip;
				}
			} break;

			default:
				dymple_log_error(dymple_log_category_relocations, "Unknown relocation instruction opcode: %u\n", opcode);
				sys_abort();
		}

		if (done) {
			break;
		}
	}

out:
	return status;
};

static ferr_t dymple_relocate_image_perform_bind(dymple_image_t* image, dymple_relocation_info_t* info) {
	ferr_t status = ferr_ok;
	dymple_bind_info_t bind_info = {0};

	size_t uleb_size = 0;

	for (size_t i = 0; i < info->bind_instructions_size; /* handled in the body */) {
		macho_rebase_opcode_t opcode = macho_relocation_instruction_get_opcode(((char*)info->bind_instructions)[i]);
		uint8_t immediate = macho_relocation_instruction_get_immediate(((char*)info->bind_instructions)[i]);
		bool done = false;

		++i;

		switch (opcode) {
			case macho_bind_opcode_done: {
				done = true;
			} break;

			case macho_bind_opcode_set_dylib_ordinal_immediate: {
				bind_info.library_ordinal = immediate;
			} break;

			case macho_bind_opcode_set_dylib_ordinal_uleb: {
				status = dymple_leb128_decode_unsigned(&info->bind_instructions[i], info->bind_instructions_size - i, &bind_info.library_ordinal, &uleb_size);
				if (status != ferr_ok) {
					goto out;
				}
				i += uleb_size;
			} break;

			case macho_bind_opcode_set_dylib_special_immediate: {
				// TODO
				dymple_log_error(dymple_log_category_relocations, "Unimplemented bind instruction: set dylib special immediate\n");
				sys_abort();
			} break;

			case macho_bind_opcode_set_symbol_trailing_flags: {
				bind_info.symbol_name = &info->bind_instructions[i];
				bind_info.symbol_name_length = simple_strlen(bind_info.symbol_name);
				bind_info.flags = immediate;

				// advance the index by the string length plus 1
				// to skip the entire string along with the null-terminator
				i += bind_info.symbol_name_length + 1;
			} break;

			case macho_bind_opcode_set_type_immediate: {
				bind_info.relocation_type = immediate;
			} break;

			case macho_bind_opcode_set_addend_sleb: {
				status = dymple_leb128_decode_signed(&info->bind_instructions[i], info->bind_instructions_size - i, &bind_info.addend, &uleb_size);
				if (status != ferr_ok) {
					goto out;
				}
				i += uleb_size;
			} break;

			case macho_bind_opcode_set_segment_immediate_and_offset_uleb: {
				bind_info.segment_index = immediate;

				status = dymple_leb128_decode_unsigned(&info->bind_instructions[i], info->bind_instructions_size - i, &bind_info.segment_offset, &uleb_size);
				if (status != ferr_ok) {
					goto out;
				}
				i += uleb_size;
			} break;

			case macho_bind_opcode_add_address_uleb: {
				uintmax_t add_addr = 0;

				status = dymple_leb128_decode_unsigned(&info->bind_instructions[i], info->bind_instructions_size - i, &add_addr, &uleb_size);
				if (status != ferr_ok) {
					goto out;
				}
				i += uleb_size;

				bind_info.segment_offset += add_addr;
			} break;

			case macho_bind_opcode_perform_bind: {
				status = dymple_perform_bind(image, &bind_info, NULL);
				if (status != ferr_ok) {
					goto out;
				}

				bind_info.segment_offset += sizeof(void*);
			} break;

			case macho_bind_opcode_perform_bind_add_address_uleb: {
				uintmax_t add_addr = 0;

				status = dymple_perform_bind(image, &bind_info, NULL);
				if (status != ferr_ok) {
					goto out;
				}

				bind_info.segment_offset += sizeof(void*);

				status = dymple_leb128_decode_unsigned(&info->bind_instructions[i], info->bind_instructions_size - i, &add_addr, &uleb_size);
				if (status != ferr_ok) {
					goto out;
				}
				i += uleb_size;

				bind_info.segment_offset += add_addr;
			} break;

			case macho_bind_opcode_perform_bind_add_address_immediate_scaled: {
				status = dymple_perform_bind(image, &bind_info, NULL);
				if (status != ferr_ok) {
					goto out;
				}

				bind_info.segment_offset += sizeof(void*) + immediate * sizeof(void*);
			} break;

			case macho_bind_opcode_perform_bind_uleb_times_skipping_uleb: {
				uintmax_t times = 0;
				uintmax_t skip = 0;

				status = dymple_leb128_decode_unsigned(&info->bind_instructions[i], info->bind_instructions_size - i, &times, &uleb_size);
				if (status != ferr_ok) {
					goto out;
				}
				i += uleb_size;

				status = dymple_leb128_decode_unsigned(&info->bind_instructions[i], info->bind_instructions_size - i, &skip, &uleb_size);
				if (status != ferr_ok) {
					goto out;
				}
				i += uleb_size;

				for (uintmax_t j = 0; j < times; ++j) {
					status = dymple_perform_bind(image, &bind_info, NULL);
					if (status != ferr_ok) {
						goto out;
					}

					bind_info.segment_offset += sizeof(void*) + skip;
				}
			} break;

			case macho_bind_opcode_threaded: {
				// TODO
				dymple_log_error(dymple_log_category_relocations, "Unimplemented bind instruction: threaded\n");
				sys_abort();
			} break;

			default:
				dymple_log_error(dymple_log_category_relocations, "Unknown rebind instruction opcode: %u\n", opcode);
				sys_abort();
		}

		if (done) {
			break;
		}
	}

out:
	return status;
};

ferr_t dymple_relocate_image(dymple_image_t* image, dymple_relocation_info_t* info) {
	ferr_t status = ferr_ok;

	dymple_log_debug(dymple_log_category_relocations, "Relocating image %.*s\n", (int)image->name_length, image->name);

	status = dymple_relocate_image_perform_rebase(image, info);
	if (status != ferr_ok) {
		goto out;
	}

	status = dymple_relocate_image_perform_bind(image, info);
	if (status != ferr_ok) {
		goto out;
	}

out:
	return status;
};

static ferr_t dymple_read_lazy_bind_info(dymple_image_t* image, size_t lazy_info_offset, dymple_bind_info_t* out_lazy_bind_info) {
	ferr_t status = ferr_ok;
	size_t uleb_size = 0;

	if (!out_lazy_bind_info) {
		status = ferr_invalid_argument;
		goto out;
	}

	if (!image->lazy_bind_instructions || lazy_info_offset >= image->lazy_bind_instructions_size) {
		dymple_log_debug(dymple_log_category_relocations, "lazy bind instructions = %p; lazy bind instructions size = %zu; lazy info offset = %zu\n", image->lazy_bind_instructions, image->lazy_bind_instructions_size, lazy_info_offset);
		status = ferr_no_such_resource;
		goto out;
	}

	simple_memset(out_lazy_bind_info, 0, sizeof(*out_lazy_bind_info));

	// the default relocation type is "pointer"
	out_lazy_bind_info->relocation_type = macho_relocation_type_pointer;

	for (size_t i = lazy_info_offset; i < image->lazy_bind_instructions_size; /* handled in the body */) {
		macho_rebase_opcode_t opcode = macho_relocation_instruction_get_opcode(((char*)image->lazy_bind_instructions)[i]);
		uint8_t immediate = macho_relocation_instruction_get_immediate(((char*)image->lazy_bind_instructions)[i]);

		++i;

		switch (opcode) {
			case macho_bind_opcode_set_dylib_ordinal_immediate: {
				out_lazy_bind_info->library_ordinal = immediate;
			} break;

			case macho_bind_opcode_set_dylib_ordinal_uleb: {
				status = dymple_leb128_decode_unsigned(&image->lazy_bind_instructions[i], image->lazy_bind_instructions_size - i, &out_lazy_bind_info->library_ordinal, &uleb_size);
				if (status != ferr_ok) {
					goto out;
				}
				i += uleb_size;
			} break;

			case macho_bind_opcode_set_dylib_special_immediate: {
				// TODO
				dymple_log_error(dymple_log_category_relocations, "Unimplemented bind instruction: set dylib special immediate\n");
				sys_abort();
			} break;

			case macho_bind_opcode_set_symbol_trailing_flags: {
				out_lazy_bind_info->symbol_name = &image->lazy_bind_instructions[i];
				out_lazy_bind_info->symbol_name_length = simple_strlen(out_lazy_bind_info->symbol_name);
				out_lazy_bind_info->flags = immediate;

				// advance the index by the string length plus 1
				// to skip the entire string along with the null-terminator
				i += out_lazy_bind_info->symbol_name_length + 1;
			} break;

			case macho_bind_opcode_set_type_immediate: {
				out_lazy_bind_info->relocation_type = immediate;
			} break;

			case macho_bind_opcode_set_addend_sleb: {
				status = dymple_leb128_decode_signed(&image->lazy_bind_instructions[i], image->lazy_bind_instructions_size - i, &out_lazy_bind_info->addend, &uleb_size);
				if (status != ferr_ok) {
					goto out;
				}
				i += uleb_size;
			} break;

			case macho_bind_opcode_set_segment_immediate_and_offset_uleb: {
				out_lazy_bind_info->segment_index = immediate;

				status = dymple_leb128_decode_unsigned(&image->lazy_bind_instructions[i], image->lazy_bind_instructions_size - i, &out_lazy_bind_info->segment_offset, &uleb_size);
				if (status != ferr_ok) {
					goto out;
				}
				i += uleb_size;
			} break;

			case macho_bind_opcode_add_address_uleb: {
				uintmax_t add_addr = 0;

				status = dymple_leb128_decode_unsigned(&image->lazy_bind_instructions[i], image->lazy_bind_instructions_size - i, &add_addr, &uleb_size);
				if (status != ferr_ok) {
					goto out;
				}
				i += uleb_size;

				out_lazy_bind_info->segment_offset += add_addr;
			} break;

			case macho_bind_opcode_perform_bind: {
				// Apple's dyld says that old apps sometimes required multiple instructions to be bound at once,
				// but since Anillo OS tells the linker we're using a new SDK version, we shouldn't ever see those cases in dymple.
				// therefore, if the next instruction after a "do bind" instruction isn't a "done" instruction, the image is invalid.
				if (i < image->lazy_bind_instructions_size && macho_relocation_instruction_get_opcode(((char*)image->lazy_bind_instructions)[i]) != macho_bind_opcode_done) {
					status = ferr_invalid_argument;
					goto out;
				}

				status = ferr_ok;
				goto out;
			} break;
		}
	}

out:
	return status;
};

void* dymple_bind_stub(dymple_stub_binding_info_t* stub_binding_info) {
	dymple_image_t* image = NULL;
	dymple_bind_info_t lazy_bind_info;
	void* bound_symbol_address = NULL;
	ferr_t status = ferr_ok;

	// we have to acquire the API lock to prevent anyone from modifying the global state while we're trying to bind this symbol
	dymple_api_lock();

	dymple_log_debug(dymple_log_category_relocations, "Image handle = %p; lazy binding info offset = %llu\n", stub_binding_info->image_handle, stub_binding_info->lazy_binding_info_offset);

	if (!*stub_binding_info->image_handle) {
		// if we don't have the image handle saved yet, look it up
		dymple_log_debug(dymple_log_category_relocations, "Image handle not saved yet; looking it up...\n");
		image = dymple_image_containing_address(stub_binding_info->image_handle);
		if (!image) {
			dymple_log_error(dymple_log_category_relocations, "Image could not be found\n");
			goto out;
		}
		// now save it
		*stub_binding_info->image_handle = image;
	} else {
		image = *stub_binding_info->image_handle;
	}

	dymple_log_debug(dymple_log_category_relocations, "Found image %.*s\n", (int)image->name_length, image->name);

	status = dymple_read_lazy_bind_info(image, stub_binding_info->lazy_binding_info_offset, &lazy_bind_info);
	if (status != ferr_ok) {
		dymple_log_debug(dymple_log_category_resolution, "Couldn't find lazy binding info; status = %d, \"%s\", \"%s\"\n", status, ferr_name(status), ferr_description(status));
		goto out;
	}

	status = dymple_perform_bind(image, &lazy_bind_info, &bound_symbol_address);
	if (status != ferr_ok) {
		dymple_log_debug(dymple_log_category_resolution, "Couldn't perform lazy bind; status = %d, \"%s\", \"%s\"\n", status, ferr_name(status), ferr_description(status));
		goto out;
	}

	dymple_log_debug(dymple_log_category_resolution, "Lazily bound to %p\n", bound_symbol_address);

out:
	dymple_api_unlock();
out_unlocked:
	return bound_symbol_address;
};
