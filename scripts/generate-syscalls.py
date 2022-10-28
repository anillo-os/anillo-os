#!/usr/bin/env python3

import os
import errno
import sys
import importlib
import io

SCRIPT_DIR = os.path.dirname(__file__)
SOURCE_ROOT = os.path.join(SCRIPT_DIR, '..')

INDEX_HEADER_GUARD_NAME = '_GEN_FERRO_SYSCALL_INDEX_H_'
HANDLER_DECLARATIONS_HEADER_GUARD_NAME = '_GEN_FERRO_SYSCALL_HANDLERS_H_'
TABLE_HEADER_GUARD_NAME = '_GEN_FERRO_SYSCALL_TABLE_H_'
WRAPPERS_HEADER_GUARD_NAME = '_GEN_FERRO_SYSCALL_WRAPPERS_H_'

sys.path.append(os.path.join(SOURCE_ROOT, 'scripts'))
import anillo_util

if len(sys.argv) != 10:
	print('Usage: ' + sys.argv[0] + ' <architecture> <index-header-include> <declarations-header-include> <wrappers-header-include> <output-index-header> <output-handler-declarations-header> <output-table-header> <output-wrappers-header> <output-wrappers-source>')
	sys.exit(1)

ARCH = sys.argv[1]
INDEX_HEADER_INCLUDE = sys.argv[2]
DECLARATIONS_HEADER_INCLUDE = sys.argv[3]
WRAPPERS_HEADER_INCLUDE = sys.argv[4]
OUTPUT_INDEX_PATH = sys.argv[5]
OUTPUT_HANDLER_DECLARATIONS_PATH = sys.argv[6]
OUTPUT_TABLE_PATH = sys.argv[7]
OUTPUT_WRAPPERS_HEADER_PATH = sys.argv[8]
OUTPUT_WRAPPERS_SOURCE_PATH = sys.argv[9]

ARCH_MODULE_DIR = os.path.join(SOURCE_ROOT, 'resources', 'syscalls')
ARCH_MODULE_PATH = os.path.join(ARCH_MODULE_DIR, ARCH + '.py')

sys.path.append(ARCH_MODULE_DIR)
syscalls_module = importlib.import_module(ARCH)
sys.path.pop()

syscalls_module.syscalls.sort_syscalls()
syscalls_module.syscalls.validate_syscalls()

anillo_util.mkdir_p(os.path.dirname(OUTPUT_INDEX_PATH))
anillo_util.mkdir_p(os.path.dirname(OUTPUT_HANDLER_DECLARATIONS_PATH))
anillo_util.mkdir_p(os.path.dirname(OUTPUT_TABLE_PATH))
anillo_util.mkdir_p(os.path.dirname(OUTPUT_WRAPPERS_HEADER_PATH))
anillo_util.mkdir_p(os.path.dirname(OUTPUT_WRAPPERS_SOURCE_PATH))

with io.open(OUTPUT_INDEX_PATH, 'w', newline='\n') as outfile:
	outfile.write(f'#ifndef {INDEX_HEADER_GUARD_NAME}\n#define {INDEX_HEADER_GUARD_NAME}\n\n')

	for syscall in syscalls_module.syscalls:
		outfile.write(f'#define FERRO_SYSCALL_{syscall.name} {str(syscall.number)}\n')

	outfile.write(f'\n#endif // {INDEX_HEADER_GUARD_NAME}\n')

with io.open(OUTPUT_HANDLER_DECLARATIONS_PATH, 'w', newline='\n') as outfile:
	outfile.write(f'#ifndef {HANDLER_DECLARATIONS_HEADER_GUARD_NAME}\n#define {HANDLER_DECLARATIONS_HEADER_GUARD_NAME}\n\n')

	outfile.write('#include <stdint.h>\n')
	outfile.write('#include <stdbool.h>\n\n')

	outfile.write('#include <ferro/base.h>\n')
	outfile.write('#include <ferro/error.h>\n')
	outfile.write('#include <ferro/api.h>\n')
	outfile.write(f'#include <{INDEX_HEADER_INCLUDE}>\n\n')

	outfile.write('FERRO_DECLARATIONS_BEGIN;\n\n')

	for enum in syscalls_module.enums:
		outfile.write(f'typedef {syscalls_module.syscall_type_to_c_type(enum.type, True)} fsyscall_{enum.name}_t;\n')

	if len(syscalls_module.enums) > 0:
		outfile.write('\n')

	for enum in syscalls_module.enums:
		values_string = ''.join([f'\tfsyscall_{enum.prefix}_{value.name} = {value.value},\n' for value in enum.values])
		if len(values_string) == 0:
			# to prevent the compiler from complaining about an empty enum
			values_string = f'fsyscall_{enum.prefix}_xxx_dummy_value,\n'
		values_string = '\n' + values_string
		outfile.write(f'enum fsyscall_{enum.name} {{{values_string}}};\n\n')

	for structure in syscalls_module.structures:
		outfile.write(f'typedef struct fsyscall_{structure.name} fsyscall_{structure.name}_t;\n')

	if len(syscalls_module.structures) > 0:
		outfile.write('\n')

	for structure in syscalls_module.structures:
		members_string = ''.join([f'\t{syscalls_module.syscall_type_to_c_type(member.type, True)} {member.name};\n' for member in structure.members])
		if len(members_string) != 0:
			members_string = '\n' + members_string
		outfile.write(f'struct fsyscall_{structure.name} {{{members_string}}};\n\n')

	for syscall in syscalls_module.syscalls:
		syscall_parameters = 'void'

		if len(syscall.parameters) > 0:
			syscall_parameters = ''
			is_first = True

			for param in syscall.parameters:
				if is_first:
					is_first = False
				else:
					syscall_parameters += ', '

				syscall_parameters += f'{syscalls_module.syscall_type_to_c_type(param.type, True)}{" " + param.name if param.name else ""}'

		outfile.write(f'ferr_t fsyscall_handler_{syscall.name}({syscall_parameters});\n')

	outfile.write('\nFERRO_DECLARATIONS_END;\n')

	outfile.write(f'\n#endif // {HANDLER_DECLARATIONS_HEADER_GUARD_NAME}\n')

with io.open(OUTPUT_TABLE_PATH, 'w', newline='\n') as outfile:
	outfile.write(f'#ifndef {TABLE_HEADER_GUARD_NAME}\n#define {TABLE_HEADER_GUARD_NAME}\n\n')

	outfile.write('#include <ferro/userspace/syscalls.h>\n')
	outfile.write(f'#include <{DECLARATIONS_HEADER_INCLUDE}>\n\n')

	outfile.write('FERRO_DECLARATIONS_BEGIN;\n\n')

	outfile.write('ferr_t fsyscall_handler_lookup_error(uint64_t syscall_number);\n\n')

	outfile.write('const fsyscall_table_t fsyscall_table_standard = {\n')
	outfile.write(f'\t.count = {len(syscalls_module.syscalls) + 1},\n')
	outfile.write('\t.handlers = {\n')
	outfile.write('\t\t[0] = fsyscall_handler_lookup_error,\n')

	for syscall in syscalls_module.syscalls:
		outfile.write(f'\t\t[{syscall.number}] = fsyscall_handler_{syscall.name},\n')

	outfile.write('\t},\n')
	outfile.write('};\n')

	outfile.write('\nFERRO_DECLARATIONS_END;\n')

	outfile.write(f'\n#endif // {TABLE_HEADER_GUARD_NAME}\n')

with io.open(OUTPUT_WRAPPERS_HEADER_PATH, 'w', newline='\n') as outfile:
	outfile.write(f'#ifndef {WRAPPERS_HEADER_GUARD_NAME}\n#define {WRAPPERS_HEADER_GUARD_NAME}\n\n')

	outfile.write('#include <stdint.h>\n')
	outfile.write('#include <stdbool.h>\n\n')

	outfile.write('#include <ferro/base.h>\n')
	outfile.write('#include <ferro/error.h>\n')
	outfile.write('#include <ferro/api.h>\n')
	outfile.write(f'#include <{INDEX_HEADER_INCLUDE}>\n\n')

	outfile.write('FERRO_DECLARATIONS_BEGIN;\n\n')

	for enum in syscalls_module.enums:
		outfile.write(f'typedef {syscalls_module.syscall_type_to_c_type(enum.type, False)} libsyscall_{enum.name}_t;\n')

	if len(syscalls_module.enums) > 0:
		outfile.write('\n')

	for enum in syscalls_module.enums:
		values_string = ''.join([f'\tlibsyscall_{enum.prefix}_{value.name} = {value.value},\n' for value in enum.values])
		if len(values_string) == 0:
			# to prevent the compiler from complaining about an empty enum
			values_string = f'fsyscall_{enum.prefix}_xxx_dummy_value,\n'
		values_string = '\n' + values_string
		outfile.write(f'enum libsyscall_{enum.name} {{{values_string}}};\n\n')

	for structure in syscalls_module.structures:
		outfile.write(f'typedef struct libsyscall_{structure.name} libsyscall_{structure.name}_t;\n')

	if len(syscalls_module.structures) > 0:
		outfile.write('\n')

	for structure in syscalls_module.structures:
		members_string = ''.join([f'\t{syscalls_module.syscall_type_to_c_type(member.type, False)} {member.name};\n' for member in structure.members])
		if len(members_string) != 0:
			members_string = '\n' + members_string
		outfile.write(f'struct libsyscall_{structure.name} {{{members_string}}};\n\n')

	for syscall in syscalls_module.syscalls:
		syscall_parameters = 'void'

		if len(syscall.parameters) > 0:
			syscall_parameters = ''
			is_first = True

			for param in syscall.parameters:
				if is_first:
					is_first = False
				else:
					syscall_parameters += ', '

				syscall_parameters += f'{syscalls_module.syscall_type_to_c_type(param.type, False)}{" " + param.name if param.name else ""}'

		outfile.write(f'ferr_t libsyscall_wrapper_{syscall.name}({syscall_parameters});\n')

	outfile.write('\nFERRO_DECLARATIONS_END;\n')

	outfile.write(f'\n#endif // {WRAPPERS_HEADER_GUARD_NAME}\n')

with io.open(OUTPUT_WRAPPERS_SOURCE_PATH, 'w', newline='\n') as outfile:
	outfile.write(f'#include <{WRAPPERS_HEADER_INCLUDE}>\n\n')

	outfile.write('ferr_t libsyscall_invoke(uint64_t syscall_number, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5, uint64_t arg6);\n')

	for syscall in syscalls_module.syscalls:
		syscall_parameters = 'void'
		syscall_arguments = ''

		if len(syscall.parameters) > 0:
			syscall_parameters = ''
			is_first = True
			index = 0

			for param in syscall.parameters:
				if is_first:
					is_first = False
				else:
					syscall_parameters += ', '

				param_name = param.name if param.name else f'arg{index}'

				syscall_parameters += f'{syscalls_module.syscall_type_to_c_type(param.type, False)} {param_name}'
				syscall_arguments += f', (uint64_t){param_name}'

				index += 1

			while index < 6:
				syscall_arguments += ', (uint64_t)0'
				index += 1

		outfile.write(f'\nferr_t libsyscall_wrapper_{syscall.name}({syscall_parameters}) {{\n')
		outfile.write(f'\treturn libsyscall_invoke({syscall.number}{syscall_arguments});\n')
		outfile.write('};\n')
