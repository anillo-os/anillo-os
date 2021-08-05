#include <ferro/bootstrap/uefi/wrappers.h>
#include <ferro/bits.h>

#define max(a, b) ({ \
		__typeof__(a) _a = (a); \
		__typeof__(b) _b = (b); \
		(_a > _b) ? _a : _b; \
	})

fuefi_status_t errstat = fuefi_status_ok;
int errno = 0;

static fuefi_image_handle_t fuefi_image_handle = NULL;
static fuefi_system_table_t* fuefi_system_table = NULL;
static fuefi_loaded_image_protocol_t* fuefi_image_protocol = NULL;
static fuefi_simple_filesystem_protocol_t* fuefi_efi_fs = NULL;
static fuefi_file_protocol_t* fuefi_efi_root = NULL;
static fuefi_graphics_output_protocol_t* fuefi_graphics_protocol = NULL;

void* malloc(size_t byte_size) {
	void* addr = NULL;
	errstat = fuefi_system_table->boot_services->allocate_pool(fuefi_memory_type_loader_data, byte_size, &addr);
	return (errstat == fuefi_status_ok) ? addr : NULL;
};

void free(void* memory) {
	(void)fuefi_system_table->boot_services->free_pool(&memory);
};

void* mmap(void* address, size_t length, int protection, int flags, int fd, off_t offset) {
	fuefi_memory_allocation_type_t alloc_type = fuefi_memory_allocation_type_any_pages;

	if (fd != -1 || offset != 0) {
		return MAP_FAILED;
	}

	if (!(flags & MAP_PRIVATE) || !(flags & MAP_ANON)) {
		return MAP_FAILED;
	}

	if (flags & MAP_FIXED) {
		alloc_type = fuefi_memory_allocation_type_fixed_address;
	}

	errstat = fuefi_system_table->boot_services->allocate_pages(alloc_type, fuefi_memory_type_loader_data, (length + 0xfff) / 0x1000, &address);

	return (errstat == fuefi_status_ok) ? address : MAP_FAILED;
};

int munmap(void* address, size_t length) {
	errstat = fuefi_system_table->boot_services->free_pages(address, (length + 0xfff) / 0x1000);
	return (errstat == fuefi_status_ok) ? 0 : -1;
};

int putchar(int character) {
	const wchar_t string[] = { character, '\0' };

	if (character == '\n') {
		if (putchar('\r') != '\r') {
			return EOF;
		}
	}

	errstat = fuefi_system_table->console_output->output_string(fuefi_system_table->console_output, string);

	return (errstat == fuefi_status_ok) ? character : EOF;
};

int printf(const char* format, ...) {
	int result;
	va_list args;

	va_start(args, format);
	result = vprintf(format, args);
	va_end(args);

	return result;
};

// 32 characters is enough for all three of these variations

static size_t print_hex(uintmax_t value, bool uppercase) {
	size_t written = 0;
	char buffer[32] = {0};

	if (value == 0) {
		if (putchar('0') < 0) {
			return -1;
		}
		return 1;
	} else {
		while (value > 0) {
			char digit = (char)(value % 16);
			if (digit < 10) {
				buffer[written++] = digit + '0';
			} else {
				buffer[written++] = (digit - 10) + (uppercase ? 'A' : 'a');
			}
			value /= 16;
		}
	}

	// the buffer is in reverse order, so print it in reverse (to get it forwards)
	for (size_t i = written; i > 0; --i) {
		if (putchar(buffer[i - 1]) < 0) {
			return -1;
		}
	}

	return written;
};

static size_t print_octal(uintmax_t value) {
	size_t written = 0;
	char buffer[32] = {0};

	if (value == 0) {
		if (putchar('0') < 0) {
			return -1;
		}
		return 1;
	} else {
		while (value > 0) {
			buffer[written++] = (char)(value % 8) + '0';
			value /= 8;
		}
	}

	// the buffer is in reverse order, so print it in reverse (to get it forwards)
	for (size_t i = written; i > 0; --i) {
		if (putchar(buffer[i - 1]) < 0) {
			return -1;
		}
	}

	return written;
};

static size_t print_decimal(uintmax_t value) {
	size_t written = 0;
	char buffer[32] = {0};

	if (value == 0) {
		if (putchar('0') < 0) {
			return -1;
		}
		return 1;
	} else {
		while (value > 0) {
			buffer[written++] = (char)(value % 10) + '0';
			value /= 10;
		}
	}

	// the buffer is in reverse order, so print it in reverse (to get it forwards)
	for (size_t i = written; i > 0; --i) {
		if (putchar(buffer[i - 1]) < 0) {
			return -1;
		}
	}

	return written;
};

int vprintf(const char* format, va_list args) {
	size_t written = 0;

	// TODO: this should absolutely be optimized to create a wchar_t buffer
	//       of at least 256 characters to allow UEFI's OutputString to write more
	//       characters at once.

	while (*format != '\0') {
		if (*format == '%') {
			++format;

			if (*format == '\0') {
				// nope, invalid format
				return -1;
			} else if (*format == '%') {
				if (putchar('%') < 0) {
					return -1;
				}
				++written;
			} else {
				// TODO: add support for more options.
				//       for now, we only support:
				//         * length
				//           * all except "L", because we don't support floating point specifiers
				//         * specifiers
				//           * all except floating point specifiers, "n", and long strings and characters (wchar_t/wint_t stuff)

				FERRO_ENUM(uint8_t, printf_length) {
					printf_length_default,
					printf_length_short_short,
					printf_length_short,
					printf_length_long,
					printf_length_long_long,
					printf_length_intmax,
					printf_length_size,
					printf_length_ptrdiff,
				};

				printf_length_t length = printf_length_default;

				if (*format == 'h') {
					++format;

					if (*format == 'h') {
						++format;

						length = printf_length_short_short;
					} else {
						length = printf_length_short;
					}
				} else if (*format == 'l') {
					++format;

					if (*format == 'l') {
						++format;

						length = printf_length_long_long;
					} else {
						length = printf_length_long;
					}
				} else if (*format == 'j') {
					++format;

					length = printf_length_intmax;
				} else if (*format == 'z') {
					++format;

					length = printf_length_size;
				} else if (*format == 't') {
					++format;

					length = printf_length_ptrdiff;
				}

				switch (*format) {
					case 'd':
					case 'i': {
						intmax_t value = 0;
						size_t result = 0;

						switch (length) {
							case printf_length_default:
							case printf_length_short_short:
							case printf_length_short:
								value = va_arg(args, int);
								break;
							case printf_length_long:
								value = va_arg(args, long int);
								break;
							case printf_length_long_long:
								value = va_arg(args, long long int);
								break;
							case printf_length_intmax:
								value = va_arg(args, intmax_t);
								break;
							case printf_length_size:
								value = va_arg(args, size_t);
								break;
							case printf_length_ptrdiff:
								value = va_arg(args, ptrdiff_t);
								break;
						}

						if (value < 0) {
							if (putchar('-') < 0) {
								return -1;
							}
							value *= -1;
							++written;
						}

						result = print_decimal(value);

						if (result == SIZE_MAX) {
							return -1;
						} else {
							written += result;
						}
					} break;

					case 'u':
					case 'o':
					case 'x':
					case 'X': {
						uintmax_t value = 0;
						size_t result = 0;

						switch (length) {
							case printf_length_default:
							case printf_length_short_short:
							case printf_length_short:
								value = va_arg(args, unsigned int);
								break;
							case printf_length_long:
								value = va_arg(args, unsigned long int);
								break;
							case printf_length_long_long:
								value = va_arg(args, unsigned long long int);
								break;
							case printf_length_intmax:
								value = va_arg(args, uintmax_t);
								break;
							case printf_length_size:
								value = va_arg(args, size_t);
								break;
							case printf_length_ptrdiff:
								value = va_arg(args, ptrdiff_t);
								break;
						}

						if (*format == 'x' || *format == 'X') {
							result = print_hex(value, *format == 'X');
						} else if (*format == 'o') {
							result = print_octal(value);
						} else {
							result = print_decimal(value);
						}

						if (result == SIZE_MAX) {
							return -1;
						} else {
							written += result;
						}
					} break;

					case 'c': {
						char value = (char)va_arg(args, int);
						if (putchar(value) < 0) {
							return -1;
						}
						++written;
					} break;

					case 's': {
						const char* value = va_arg(args, const char*);

						while (*value != '\0') {
							if (putchar(*value) < 0) {
								return -1;
							}
							++written;
							++value;
						}
					} break;

					case 'p': {
						const void* value = va_arg(args, const void*);
						size_t result = 0;

						// in reality, this should pad to 16 characters (not including "0x")
						if (putchar('0') < 0) {
							return -1;
						}
						if (putchar('x') < 0) {
							return -1;
						}
						result = print_hex((uintmax_t)(uintptr_t)value, false);

						if (result == SIZE_MAX) {
							return -1;
						} else {
							written += result;
						}
					} break;

					default: {
						// invalid format
						return -1;
					} break;
				}
			}
		} else {
			if (putchar(*format) < 0) {
				return -1;
			}
			++written;
		}

		++format;
	}

	return written;
};

size_t strlen(const char* string) {
	size_t length = 0;
	while (*string != '\0') {
		++string;
		++length;
	}
	return length;
};

FERRO_STRUCT(atw_context) {
	wchar_t* string;
	wchar_t cache[256];
};

static bool ascii_to_wide(const char* input, atw_context_t* context) {
	size_t len = strlen(input);

	if (len > (sizeof(context->cache) - 1)) {
		context->string = malloc(len + 1);
	} else {
		context->string = context->cache;
	}

	if (!context->string) {
		return false;
	}

	for (size_t i = 0; i < len; ++i) {
		context->string[i] = input[i];
	}

	context->string[len] = '\0';

	return true;
};

static void ascii_to_wide_free(atw_context_t* context) {
	if (context->string != context->cache) {
		free(context->string);
	}
};

FILE* fopen(const char* filename, const char* mode) {
	fuefi_file_protocol_t* file = NULL;
	fuefi_file_mode_t uefi_mode = 0;
	uint64_t uefi_attrs = 0;
	atw_context_t atw;

	if (!ascii_to_wide(filename, &atw)) {
		errstat = fuefi_status_invalid_parameter;
		goto out;
	}

	if (mode[0] == 'r') {
		uefi_mode = fuefi_file_mode_read;
	} else {
		errstat = fuefi_status_invalid_parameter;
		goto out;
	}

	if (mode[1] != 'b') {
		errstat = fuefi_status_invalid_parameter;
		goto out;
	}

	errstat = fuefi_efi_root->open(fuefi_efi_root, &file, atw.string, uefi_mode, uefi_attrs);
	if (errstat != fuefi_status_ok) {
		goto out;
	}

	ascii_to_wide_free(&atw);

out:
	return (errstat == fuefi_status_ok) ? (FILE*)file : NULL;
};

int fclose(FILE* file) {
	fuefi_file_protocol_t* protocol = (fuefi_file_protocol_t*)file;
	errstat = protocol->close(protocol);
	return (errstat == fuefi_status_ok) ? 0 : EOF;
};

size_t fread(void* buffer, size_t element_size, size_t element_count, FILE* file) {
	fuefi_file_protocol_t* protocol = (fuefi_file_protocol_t*)file;
	size_t size = element_size * element_count;
	errstat = protocol->read(protocol, &size, buffer);
	return size;
};

int fseek(FILE* file, long long int offset, int origin) {
	fuefi_file_protocol_t* protocol = (fuefi_file_protocol_t*)file;
	errstat = protocol->set_position(protocol, *(unsigned long long int*)&offset);
	return (errstat == fuefi_status_ok) ? 0 : -1;
};

void* memset(void* _destination, int value, size_t count) {
	char* destination = (char*)_destination;
	while (count > 0) {
		*destination = (char)value;
		++destination;
		--count;
	}
	return _destination;
};

void* memcpy(void* _destination, const void* _source, size_t count) {
	const char* source = (const char*)_source;
	char* destination = (char*)_destination;
	while (count > 0) {
		*destination = *source;
		++source;
		++destination;
		--count;
	}
	return _destination;
};

long long sysconf(int name) {
	unsigned long long result = 0;

	switch (name) {
		case _SC_FB_AVAILABLE: {
			result = !!fuefi_graphics_protocol;
		} break;

		case _SC_FB_BASE: {
			result = (uintptr_t)fuefi_graphics_protocol->mode->framebuffer_phys_addr;
		} break;

		case _SC_FB_WIDTH: {
			result = fuefi_graphics_protocol->mode->info->width;
		} break;

		case _SC_FB_HEIGHT: {
			result = fuefi_graphics_protocol->mode->info->height;
		} break;

		case _SC_FB_RED_MASK: {
			if (fuefi_graphics_protocol->mode->info->format == fuefi_graphics_pixel_format_rgb) {
				result = U32_BYTE_ZERO_MASK;
			} else if (fuefi_graphics_protocol->mode->info->format == fuefi_graphics_pixel_format_bgr) {
				result = U32_BYTE_TWO_MASK;
			} else {
				result = fuefi_graphics_protocol->mode->info->bitmask.red;
			}
		} break;

		case _SC_FB_GREEN_MASK: {
			if (fuefi_graphics_protocol->mode->info->format == fuefi_graphics_pixel_format_rgb) {
				result = U32_BYTE_ONE_MASK;
			} else if (fuefi_graphics_protocol->mode->info->format == fuefi_graphics_pixel_format_bgr) {
				result = U32_BYTE_ONE_MASK;
			} else {
				result = fuefi_graphics_protocol->mode->info->bitmask.green;
			}
		} break;

		case _SC_FB_BLUE_MASK: {
			if (fuefi_graphics_protocol->mode->info->format == fuefi_graphics_pixel_format_rgb) {
				result = U32_BYTE_TWO_MASK;
			} else if (fuefi_graphics_protocol->mode->info->format == fuefi_graphics_pixel_format_bgr) {
				result = U32_BYTE_ZERO_MASK;
			} else {
				result = fuefi_graphics_protocol->mode->info->bitmask.blue;
			}
		} break;

		case _SC_FB_RESERVED_MASK: {
			if (fuefi_graphics_protocol->mode->info->format == fuefi_graphics_pixel_format_rgb) {
				result = U32_BYTE_THREE_MASK;
			} else if (fuefi_graphics_protocol->mode->info->format == fuefi_graphics_pixel_format_bgr) {
				result = U32_BYTE_THREE_MASK;
			} else {
				result = fuefi_graphics_protocol->mode->info->bitmask.reserved;
			}
		} break;

		case _SC_FB_BIT_COUNT: {
			if (fuefi_graphics_protocol->mode->info->format == fuefi_graphics_pixel_format_rgb) {
				result = 32;
			} else if (fuefi_graphics_protocol->mode->info->format == fuefi_graphics_pixel_format_bgr) {
				result = 32;
			} else {
				result = max(
					max(
						max(
							ferro_bits_in_use_u32(fuefi_graphics_protocol->mode->info->bitmask.red), 
							ferro_bits_in_use_u32(fuefi_graphics_protocol->mode->info->bitmask.green)
						),
						ferro_bits_in_use_u32(fuefi_graphics_protocol->mode->info->bitmask.blue)
					),
					ferro_bits_in_use_u32(fuefi_graphics_protocol->mode->info->bitmask.reserved)
				);
			}
		} break;

		case _SC_FB_PIXELS_PER_SCANLINE: {
			result = fuefi_graphics_protocol->mode->info->pixels_per_scanline;
		} break;

		case _SC_IMAGE_BASE: {
			result = (uintptr_t)fuefi_image_protocol->image_base;
		} break;
	}

	return *(long long*)&result;
};

static int sysctl_bs_memory_map_info(fuefi_sysctl_bs_memory_map_info_t* info) {
	errstat = fuefi_system_table->boot_services->get_memory_map(&info->map_size, NULL, NULL, &info->descriptor_size, NULL);
	return (errstat == fuefi_status_ok || errstat == fuefi_status_buffer_too_small) ? 0 : -1;
};

static int sysctl_bs_populate_memory_map(fuefi_sysctl_bs_populate_memory_map_t* info) {
	size_t map_size = info->map_size;
	errstat = fuefi_system_table->boot_services->get_memory_map(&map_size, info->memory_map, &info->map_key, &info->descriptor_size, &info->descriptor_version);
	return (errstat == fuefi_status_ok) ? 0 : -1;
};

static int sysctl_bs_exit_boot_services(fuefi_memory_map_key_t* info) {
	errstat = fuefi_system_table->boot_services->exit_boot_services(fuefi_image_handle, *info);
	if (errstat != fuefi_status_ok) {
		fuefi_status_t saved = errstat;
		printf("ebs failed with " FUEFI_STATUS_FORMAT, errstat);
		errstat = saved;
	}
	return (errstat == fuefi_status_ok) ? 0 : -1;
};

static int sysctl_init_wrappers(fuefi_sysctl_wrappers_init_t* info) {
	fuefi_image_handle = info->image_handle;
	fuefi_system_table = info->system_table;

	errstat = fuefi_system_table->boot_services->handle_protocol(fuefi_image_handle, fuefi_guid_loaded_image_protocol, (void**)&fuefi_image_protocol);
	if (errstat != fuefi_status_ok) {
		printf("failed to load image protocol\n");
		goto out;
	}

	errstat = fuefi_system_table->boot_services->handle_protocol(fuefi_image_protocol->source_device, fuefi_guid_simple_filesystem_protocol, (void**)&fuefi_efi_fs);
	if (errstat != fuefi_status_ok) {
		printf("failed to load fs protocol\n");
		goto out;
	}

	errstat = fuefi_efi_fs->open_volume(fuefi_efi_fs, &fuefi_efi_root);
	if (errstat != fuefi_status_ok) {
		printf("failed to open efi root\n");
		goto out;
	}

	errstat = fuefi_system_table->boot_services->locate_protocol(fuefi_guid_graphics_output_protocol, NULL, (void**)&fuefi_graphics_protocol);
	if (errstat == fuefi_status_ok) {
		if (fuefi_graphics_protocol->mode->info->format == fuefi_graphics_pixel_format_blt_only) {
			fuefi_graphics_protocol = NULL;
		}
	} else {
		fuefi_graphics_protocol = NULL;
		errstat = fuefi_status_ok;
	}

out:
	return (errstat == fuefi_status_ok) ? 0 : -1;
};

int sysctl(const int* name, unsigned int name_length, void* old_data, size_t* old_data_length, void* new_data, size_t new_data_length) {
	if (name_length > 0) {
		switch (name[0]) {
			case CTL_BS: {
				if (name_length > 1) {
					switch (name[1]) {
						case BS_MEMORY_MAP_INFO: {
							if (!old_data_length) {
								return -1;
							} else {
								*old_data_length = sizeof(fuefi_sysctl_bs_memory_map_info_t);
							}

							if (old_data) {
								return sysctl_bs_memory_map_info(old_data);
							}

							return 0;
						} break;

						case BS_POPULATE_MEMORY_MAP: {
							if (!old_data_length) {
								return -1;
							} else {
								*old_data_length = sizeof(fuefi_sysctl_bs_populate_memory_map_t);
							}

							if (old_data) {
								return sysctl_bs_populate_memory_map(old_data);
							}

							return 0;
						} break;

						case BS_EXIT_BOOT_SERVICES: {
							if (!new_data) {
								return -1;
							}

							if (new_data_length < sizeof(fuefi_memory_map_key_t)) {
								return -1;
							}

							return sysctl_bs_exit_boot_services(new_data);
						} break;

						default: {
							return -1;
						} break;
					}
				}
			} break;

			case CTL_WRAPPERS: {
				if (name_length > 1) {
					switch (name[1]) {
						case WRAPPERS_INIT: {
							if (!new_data) {
								return -1;
							}

							if (new_data_length < sizeof(fuefi_sysctl_wrappers_init_t)) {
								return -1;
							}

							return sysctl_init_wrappers(new_data);
						} break;

						default: {
							return -1;
						} break;
					}
				}
			} break;

			default: {
				return -1;
			} break;
		}
	}

	return -1;
};
