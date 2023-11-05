#!/usr/bin/env python3

from lark import ast_utils
from lark.lark import Lark
from lark.visitors import Transformer, v_args
from lark.lexer import Token
import sys
from dataclasses import dataclass
from typing import Dict, List, Set, Tuple, cast
from enum import IntEnum
import argparse
import os
from ast import literal_eval
import json
import copy

this_module = sys.modules[__name__]

class Type:
	pass

class Realm(IntEnum):
	INVALID  = 0
	CHILDREN = 1
	PARENT   = 2
	LOCAL    = 3
	GLOBAL   = 4

names: Set[str] = set()
next_type_id: int = 0
type_to_id: Dict[Type, int] = dict()
id_to_type: Dict[int, Type] = dict()
interface_names: Set[str] = set()
struct_types: Dict[str, Type] = dict()
max_type_members: int = 0
max_type_params: int = 0
root_interface_name: str | None = None
default_server_name: str | None = None
default_realm: Realm | None = None
default_client_realm: Realm | None = None

def name_to_type(name: str) -> Type:
	type: Type | None = None

	if name in BasicTypeTag.__members__:
		type = BasicType(BasicTypeTag[name])
	elif name in interface_names:
		type = BasicType(BasicTypeTag.proxy)
	elif name in struct_types:
		type = struct_types[name]
	else:
		print(f"Undefined type: {name}", file=sys.stderr)
		exit(1)

	return type

def ensure_type_to_id(type: Type) -> int:
	global next_type_id
	global max_type_members
	global max_type_params

	if type in type_to_id:
		return type_to_id[type]

	type_id = next_type_id
	next_type_id += 1

	type_to_id[type] = type_id
	id_to_type[type_id] = type

	if isinstance(type, StructureType):
		max_type_members = max(max_type_members, len(type.members))

	if isinstance(type, FunctionType):
		max_type_params = max(max_type_params, len(type.parameters))

	return type_id

#
# AST
#

class Direction(IntEnum):
	IN = 1
	OUT = 2

class BasicTypeTag(IntEnum):
	u8    =  0
	u16   =  1
	u32   =  2
	u64   =  3
	i8    =  4
	i16   =  5
	i32   =  6
	i64   =  7
	bool  =  8
	f32   =  9
	f64   = 10
	data  = 11
	proxy = 12,
	channel = 13,
	server_channel = 14,

BASIC_TYPE_TAG_TO_NATIVE_TYPE = {
	BasicTypeTag.u8    : 'uint8_t',
	BasicTypeTag.u16   : 'uint16_t',
	BasicTypeTag.u32   : 'uint32_t',
	BasicTypeTag.u64   : 'uint64_t',
	BasicTypeTag.i8    : 'int8_t',
	BasicTypeTag.i16   : 'int16_t',
	BasicTypeTag.i32   : 'int32_t',
	BasicTypeTag.i64   : 'int64_t',
	BasicTypeTag.bool  : 'bool',
	BasicTypeTag.f32   : 'float',
	BasicTypeTag.f64   : 'double',
	BasicTypeTag.data  : 'sys_data_t*',
	BasicTypeTag.proxy : 'spooky_proxy_t*',
	BasicTypeTag.channel : 'sys_channel_t*',
	BasicTypeTag.server_channel : 'sys_server_channel_t*',
}

def type_is_refcounted(type: BasicTypeTag) -> bool:
	return type == BasicTypeTag.data or type == BasicTypeTag.proxy or type == BasicTypeTag.channel or type == BasicTypeTag.server_channel

def type_is_consumed(type: BasicTypeTag) -> bool:
	return type == BasicTypeTag.channel or type == BasicTypeTag.server_channel

def type_is_libsys(type: BasicTypeTag) -> bool:
	return type == BasicTypeTag.channel or type == BasicTypeTag.server_channel

def type_to_native_for_source(type: Type) -> str:
	if isinstance(type, BasicType):
		return BASIC_TYPE_TAG_TO_NATIVE_TYPE[type.tag]
	elif isinstance(type, StructureType):
		return f'struct _spookygen_struct_{type_to_id[type]}'
	raise TypeError

class _AST(ast_utils.Ast):
	pass

class _Entry(_AST):
	pass

class Decorations(_AST, ast_utils.AsList):
	decorations: Set[str]

	def __init__(self, decorations: List[str]) -> None:
		super().__init__()
		self.decorations = set(decorations)

class Parameter(_AST):
	name: str
	direction: Direction
	type: Type
	type_id: int

	def __init__(self, name: str, direction: Direction, type_tuple: Tuple[int, Type]) -> None:
		super().__init__()
		self.name = name
		self.direction = direction
		self.type_id = type_tuple[0]
		self.type = type_tuple[1]

	def __eq__(self, other: object) -> bool:
		if not isinstance(other, Parameter):
			return False
		return self.direction == other.direction and self.type_id == other.type_id

	def __hash__(self) -> int:
		return hash((self.direction, self.type_id))

	def __repr__(self) -> str:
		return f'Parameter(name={repr(self.name)}, direction={repr(self.direction)}, type_id={repr(self.type_id)})'

@dataclass
class BasicType(Type):
	tag: BasicTypeTag

	def __eq__(self, other: object) -> bool:
		if not isinstance(other, BasicType):
			return False
		return self.tag == other.tag

	def __hash__(self) -> int:
		return hash(self.tag)

@dataclass
class StructureType(Type):
	name: str
	members: List[int]

	def __eq__(self, other: object) -> bool:
		if not isinstance(other, StructureType):
			return False
		return self.members == other.members

	def __hash__(self) -> int:
		return hash(tuple(self.members))

class FunctionType(Type):
	decorations: Set[str]
	parameters: List[Parameter]

	def __init__(self, decorations: Decorations, *params: Parameter) -> None:
		super().__init__()
		self.decorations = decorations.decorations if decorations != None else set()
		self.parameters = list(params)

	def __repr__(self) -> str:
		return f"FunctionType(decorations={repr(self.decorations)}, parameters={repr(self.parameters)})"

	def __eq__(self, other: object) -> bool:
		if not isinstance(other, FunctionType):
			return False
		return self.decorations == other.decorations and self.parameters == other.parameters

	def __hash__(self) -> int:
		return hash((tuple(self.decorations), tuple(self.parameters)))

class Function(_Entry):
	name: str
	decorations: Set[str]
	parameters: List[Parameter]
	type_id: int

	def __init__(self, decorations: Decorations, name: str, *params: Parameter) -> None:
		super().__init__()

		if name in names:
			print(f"Conflicating name: {name}", file=sys.stderr)
			exit(1)

		names.add(name)

		self.decorations = decorations.decorations if decorations != None else set()
		self.name = name
		self.parameters = list(params)
		self.type_id = ensure_type_to_id(FunctionType(decorations, *params))

	def __repr__(self) -> str:
		return f"Function(name={repr(self.name)}, decorations={repr(self.decorations)}, parameters={repr(self.parameters)}, type_id={repr(self.type_id)})"

class Member(_AST):
	name: str
	type_name: str
	type_id: int

	def __init__(self, name: str, type_name: str) -> None:
		super().__init__()

		self.name = name
		self.type_name = type_name
		self.type_id = ensure_type_to_id(name_to_type(type_name))

	def __repr__(self) -> str:
		return f"Member(name={repr(self.name)}, type_id={repr(self.type_id)})"

class Structure(_Entry):
	name: str
	members: List[Member]
	type: StructureType
	type_id: int

	def __init__(self, name: str, *members: Member) -> None:
		super().__init__()

		if name in names:
			print(f"Conflicating name: {name}", file=sys.stderr)
			exit(1)

		names.add(name)

		self.name = name
		self.members = list(members)

		self.type = StructureType(self.name, [ x.type_id for x in self.members ])
		self.type_id = ensure_type_to_id(self.type)
		struct_types[name] = self.type

	def __repr__(self) -> str:
		return f"Structure(name={repr(self.name)}, members={repr(self.members)}, type_id={self.type_id})"

class Interface(_Entry):
	name: str
	functions: List[Function]

	def __init__(self, name: str, *functions: Function) -> None:
		super().__init__()

		self.name = name
		self.functions = list(functions)

		interface_names.add(name)

	def __repr__(self) -> str:
		return f"Interface(name={repr(self.name)}, functions={repr(self.functions)})"

class ToAST(Transformer):
	def NAME(self, tok: Token):
		return tok.value

	def DECORATION(self, tok: Token):
		return tok.value

	def DIRECTION(self, tok: Token):
		if tok.value == "in":
			return Direction.IN
		elif tok.value == "out":
			return Direction.OUT
		else:
			raise ValueError

	@v_args(inline=True)
	def type(self, name_or_decorations: str | Decorations, *params: Parameter) -> Tuple[int, Type]:
		type: Type | None = None

		if isinstance(name_or_decorations, str):
			type = name_to_type(name_or_decorations)
		else:
			type = FunctionType(name_or_decorations, *params)

		return (ensure_type_to_id(type), type)

	@v_args(inline=True)
	def interface_directive(self, name: str):
		global root_interface_name
		if root_interface_name != None:
			print("Duplicate root interface name definition", file=sys.stderr)
			exit(1)
		root_interface_name = name

	@v_args(inline=True)
	def server_name_directive(self, string: str):
		global default_server_name
		if default_server_name != None:
			print("Duplicate server name definition", file=sys.stderr)
			exit(1)
		default_server_name = literal_eval(string)

	@v_args(inline=True)
	def server_realm_directive(self, realm: str):
		global default_realm
		if default_realm != None:
			print("Duplicate server realm definition", file=sys.stderr)
		default_realm = Realm[realm.upper()]

	def start(self, s):
		return s[1:]

#
# main code
#

argparser = argparse.ArgumentParser('spookygen', description='Generate libspooky wrappers from an RPC definition file')
argparser.add_argument('--server', action='store_true', help='Generate server-side wrappers. By default, client-side wrappers are generated instead.')
argparser.add_argument('-i', '--input', required=True, help='The RPC definition file to parse')
argparser.add_argument('-s', '--source', required=True, help='A path for the resulting wrapper source file')
argparser.add_argument('-H', '--header', required=True, help='A path for the resulting wrapper header file')
args = argparser.parse_args()

lark_parser = Lark.open('spooky.lark', rel_to=__file__, maybe_placeholders=True, parser="lalr")
lark_trans = ast_utils.create_transformer(this_module, ToAST())

input_file = open(args.input)
parse_tree = lark_parser.parse(input_file.read())
input_file.close()

ast: List[Interface | Structure] = lark_trans.transform(parse_tree)

if default_realm == None:
	default_realm = Realm.GLOBAL

default_client_realm = default_realm
if default_client_realm == Realm.CHILDREN:
	default_client_realm = Realm.PARENT
elif default_client_realm == Realm.PARENT:
	default_client_realm = Realm.CHILDREN

structures: List[Structure] = [x for x in ast if isinstance(x, Structure)]
interfaces: List[Interface] = [x for x in ast if isinstance(x, Interface)]

if os.path.dirname(args.source) != '':
	os.makedirs(os.path.dirname(args.source), exist_ok=True)

if os.path.dirname(args.header) != '':
	os.makedirs(os.path.dirname(args.header), exist_ok=True)

source_file = open(args.source, 'w')
header_file = open(args.header, 'w')

header_file.writelines([
	f'#pragma once\n',
	'\n',
	f'#include <libspooky/libspooky.h>\n',
	'\n',
	f'#ifndef SPOOKYGEN_{root_interface_name}_STORAGE\n',
	f'\t#define SPOOKYGEN_{root_interface_name}_STORAGE\n',
	f'#endif // SPOOKYGEN_{root_interface_name}_STORAGE\n',
	'\n',
])

written_structs: Set[str] = set()

def write_struct(struct: Structure):
	if struct.name in written_structs:
		return

	written_structs.add(struct.name)

	header_file.write(f'struct {struct.name} {{\n')
	for member in struct.members:
		type_str = ''
		if member.type_name in BasicTypeTag.__members__:
			type_str = BASIC_TYPE_TAG_TO_NATIVE_TYPE[BasicTypeTag[member.type_name]]
		else:
			write_struct(next(x for x in structures if x.name == member.type_name))
			type_str = f'struct {member.type_name}'
		header_file.write(f'\t{type_str} {member.name};\n')
	header_file.write('};\n\n')

for structure in structures:
	write_struct(structure)

source_file.writelines([
	f'#include "{os.path.basename(args.header)}"\n',
	f'#include <libsys/libsys.h>\n',
	'\n',
	'struct _spookygen_callback_context {\n',
	'\tvoid* target;\n',
	'\tvoid* target_context;\n',
	'};\n',
	'\n',
	f'static spooky_type_t* _spookygen_types[{next_type_id}];\n',
	f'static sys_once_t _spookygen_init_token = SYS_ONCE_INITIALIZER;\n',
	'\n',
])

if args.server:
	source_file.writelines([
		'// TODO: this should be a rwlock instead, once we implement those in libsys\n'
		'static sys_mutex_t _spookygen_server_mutex = SYS_MUTEX_INIT;\n',
		'static spooky_interface_t* _spookygen_interface = NULL;\n',
		'static eve_server_channel_t* _spookygen_server_channel = NULL;\n',
		'\n',
	])
else:
	source_file.writelines([
		'// TODO: this should be a rwlock instead, once we implement those in libsys\n'
		'static sys_mutex_t _spookygen_client_mutex = SYS_MUTEX_INIT;\n',
		'static eve_channel_t* _spookygen_channel = NULL;\n',
		'\n',
		f'static ferr_t {root_interface_name}_init_explicit_locked(const char* name, size_t name_length, sys_channel_realm_t realm, eve_loop_t* loop);\n',
		'\n',
	])

for type_id in range(0, next_type_id):
	type = id_to_type[type_id]

	if not isinstance(type, StructureType):
		continue

	source_file.write(f'struct _spookygen_struct_{type_id} {{\n')
	for index, member in enumerate(type.members):
		member_type = id_to_type[member]
		source_file.write(f'\t{type_to_native_for_source(member_type)} _spookygen_member_{index};\n')
	source_file.write('};\n\n')

source_file.writelines([
	'static void _spookygen_init(void* context) {\n',
	f'\tspooky_structure_member_t members[{max_type_members}];\n',
	f'\tspooky_function_parameter_t parameters[{max_type_params}];\n',
	'\n',
])

for type_id in range(0, next_type_id):
	type = id_to_type[type_id]

	if isinstance(type, BasicType):
		source_file.write(f'\t_spookygen_types[{type_id}] = spooky_type_{type.tag.name}();\n')
	elif isinstance(type, StructureType):
		for index, member in enumerate(type.members):
			source_file.write(f'\tmembers[{index}].type = _spookygen_types[{member}];\n')
			source_file.write(f'\tmembers[{index}].offset = offsetof(struct _spookygen_struct_{type_id}, _spookygen_member_{index});\n')

		source_file.write(f'\tsys_abort_status_log(spooky_structure_create(sizeof(struct _spookygen_struct_{type_id}), members, {len(type.members)}, &_spookygen_types[{type_id}]));\n')
	elif isinstance(type, FunctionType):
		does_wait = not ('nowait' in type.decorations)

		for index, param in enumerate(type.parameters):
			source_file.write(f'\tparameters[{index}].type = _spookygen_types[{param.type_id}];\n')
			source_file.write(f'\tparameters[{index}].direction = {"spooky_function_parameter_direction_in" if param.direction == Direction.IN else "spooky_function_parameter_direction_out"};\n')

		source_file.write(f'\tsys_abort_status_log(spooky_function_create({"true" if does_wait else "false"}, parameters, {len(type.parameters)}, &_spookygen_types[{type_id}]));\n')

source_file.writelines([
	'};\n',
	'\n',
	'static void _spookygen_ensure_init(void) {\n',
	'\tsys_once(&_spookygen_init_token, _spookygen_init, NULL, 0);\n',
	'};\n',
	'\n',
])

def write_parameters(file, prefix: str, params: List[Parameter], is_first: bool, use_generic_type: bool = False):
	for index, param in enumerate(params):
		if not is_first or index != 0:
			file.write(', ')

		if isinstance(param.type, BasicType):
			file.write(BASIC_TYPE_TAG_TO_NATIVE_TYPE[param.type.tag])
			if param.direction == Direction.OUT:
				file.write('*')
			file.write(f' {param.name}')
		elif isinstance(param.type, StructureType):
			if param.direction == Direction.IN:
				file.write('const ')
			struct_name = f'_spookygen_struct_{param.type_id}' if use_generic_type else param.type.name
			file.write(f'struct {struct_name}* {param.name}')
		elif isinstance(param.type, FunctionType):
			extra_ptr = '*' if param.direction == Direction.OUT else ''
			func_type_name = f'_spookygen_type_{param.type_id}_f' if use_generic_type else f'{prefix}_{param.name}_f'
			file.write(f'{func_type_name}{extra_ptr} {param.name}, void*{extra_ptr} _context_{param.name}')

def write_param_type(prefix: str, param: Parameter):
	if not isinstance(param.type, FunctionType):
		return

	for subparam in param.type.parameters:
		write_param_type(f'{prefix}_{param.name}', subparam)

	header_file.write(f'typedef ferr_t (*{prefix}_{param.name}_f)(void* _context')
	write_parameters(header_file, f'{prefix}_{param.name}', param.type.parameters, False)
	header_file.write(');\n')

def write_function_type(type: Type):
	if not isinstance(type, FunctionType):
		return

	params = copy.deepcopy(type.parameters)

	for index, param in enumerate(params):
		param.name = f'arg{index}'
		write_function_type(param.type)

	source_file.write(f'typedef ferr_t (*_spookygen_type_{type_to_id[type]}_f)(void* _context')
	write_parameters(source_file, '', params, False, True)
	source_file.write(');\n')

for type_id in range(0, next_type_id):
	type = id_to_type[type_id]
	write_function_type(type)

for interface in interfaces:
	for function in interface.functions:
		for param in function.parameters:
			write_param_type(f'{interface.name}_{function.name}', param)

		is_root_interface = interface.name == root_interface_name

		if args.server or not is_root_interface:
			if is_root_interface:
				header_file.write(f'ferr_t {interface.name}_{function.name}_impl(void* _context')
			else:
				header_file.write(f'typedef ferr_t (*{interface.name}_{function.name}_impl_f)(void* _context')
			write_parameters(header_file, f'{interface.name}_{function.name}', function.parameters, False)
			header_file.write(');\n\n')

		if not args.server or not is_root_interface:
			header_file.write(f'SPOOKYGEN_{root_interface_name}_STORAGE LIBSPOOKY_WUR ferr_t {interface.name}_{function.name}(void* context')
			write_parameters(header_file, f'{interface.name}_{function.name}', function.parameters, False)
			header_file.write(');\n\n')

for interface in interfaces:
	if interface.name == root_interface_name:
		continue

	header_file.write(f'typedef struct {interface.name}_proxy_info {{\n')
	header_file.write('\tvoid* context;\n')
	header_file.write('\tvoid (*destructor)(void*);\n')

	for function in interface.functions:
		header_file.write(f'\t{interface.name}_{function.name}_impl_f {function.name};\n')

	header_file.write(f'}} {interface.name}_proxy_info_t;\n\n')

written_names: Set[str] = set()

# i.e. for interface implementations or input callback implementations
# invoked remotely by libspooky
def write_incoming_function_wrapper(name: str, type: FunctionType, target_name: str | None, is_root_interface: bool, raw_name: str | None):
	if name in written_names:
		return

	written_names.add(name)

	for index, param in enumerate(type.parameters):
		if isinstance(param.type, FunctionType):
			if param.direction == Direction.IN:
				write_outgoing_function_wrapper(f'_spookygen_callback_{param.type_id}', param.type, None, False, None)
			else:
				write_incoming_function_wrapper(f'_spookygen_callback_handler_{param.type_id}', param.type, None, False, None)

	source_file.writelines([
		f'static void {name}(void* context, spooky_invocation_t* invocation) {{\n',
		'\tferr_t status = ferr_ok;\n',
		f'\t_spookygen_type_{type_to_id[type]}_f target = NULL;\n',
		'\tvoid* target_context = NULL;\n',
		'\n',
		'\t_spookygen_ensure_init();\n',
		'\n',
	])

	if target_name == None:
		source_file.writelines([
			'\tif (!invocation) {\n',
			'\t\tLIBSPOOKY_WUR_IGNORE(sys_mempool_free(context));\n',
			'\t\treturn;\n',
			'\t}\n',
			'\n',
			'\ttarget = ((struct _spookygen_callback_context*)context)->target;\n',
			'\ttarget_context = ((struct _spookygen_callback_context*)context)->target_context;\n',
			'\tLIBSPOOKY_WUR_IGNORE(sys_mempool_free(context));\n\n',
		])
	elif not is_root_interface:
		source_file.writelines([
			'\tif (!invocation) {\n',
			'\t\treturn;\n',
			'\t}\n',
			'\n',
			f'\ttarget = (({interface.name}_proxy_info_t*)context)->{raw_name};\n',
			f'\ttarget_context = (({interface.name}_proxy_info_t*)context)->context;\n',
		])
	else:
		source_file.writelines([
			'\tif (!invocation) {\n',
			'\t\treturn;\n',
			'\t}\n',
			'\n',
			f'\ttarget = (void*){target_name};\n',
			'\ttarget_context = NULL;\n\n',
		])

	for index, param in enumerate(type.parameters):
		if isinstance(param.type, BasicType):
			init_str = " = NULL" if type_is_refcounted(param.type.tag) else ""
			source_file.write(f'\t{BASIC_TYPE_TAG_TO_NATIVE_TYPE[param.type.tag]} arg{index}{init_str};\n')
			if param.direction == Direction.IN:
				retain_arg = ", false" if type_is_refcounted(param.type.tag) else ""
				source_file.writelines([
					f'\tstatus = spooky_invocation_get_{param.type.tag.name}(invocation, {index}{retain_arg}, &arg{index});\n',
					'\tif (status != ferr_ok) {\n',
					'\t\tgoto out;\n',
					'\t}\n',
				])
		elif isinstance(param.type, StructureType):
			source_file.write(f'\tstruct _spookygen_struct_{param.type_id} arg{index};\n')
			if param.direction == Direction.IN:
				source_file.writelines([
					f'\tsize_t arg{index}_size = sizeof(arg{index});\n',
					f'\tstatus = spooky_invocation_get_structure(invocation, {index}, false, &arg{index}, &arg{index}_size);\n',
					'\tif (status != ferr_ok) {\n',
					'\t\tgoto out;\n',
					'\t}\n',
				])
			else:
				source_file.write(f'\tsimple_memset(&arg{index}, 0, sizeof(arg{index}));\n')
		elif isinstance(param.type, FunctionType):
			if param.direction == Direction.IN:
				source_file.writelines([
					f'\tspooky_invocation_t* arg{index}_invocation = NULL;\n',
					f'\tstatus = spooky_invocation_get_invocation(invocation, {index}, &arg{index}_invocation);\n',
					'\tif (status != ferr_ok) {\n',
					'\t\tgoto out;\n',
					'\t}\n',
				])
			else:
				source_file.writelines([
					f'\tstruct _spookygen_callback_context* arg{index}_callback_context = NULL;\n',
					f'\tstatus = sys_mempool_allocate(sizeof(*arg{index}_callback_context), NULL, (void*)&arg{index}_callback_context);\n',
					'\tif (status != ferr_ok) {\n',
					'\t\tgoto out;\n',
					'\t}\n',
					f'\targ{index}_callback_context->target = NULL;\n',
					f'\targ{index}_callback_context->target_context = NULL;\n',
				])

	source_file.write('\tstatus = target(target_context')

	for index, param in enumerate(type.parameters):
		is_input = param.direction == Direction.IN
		output_addr_of = '' if is_input else '&'
		if isinstance(param.type, BasicType):
			source_file.write(f', {output_addr_of}arg{index}')
		elif isinstance(param.type, StructureType):
			source_file.write(f', &arg{index}')
		elif isinstance(param.type, FunctionType):
			if is_input:
				source_file.write(f', _spookygen_callback_{param.type_id}, arg{index}_invocation')
			else:
				source_file.write(f', (void*)&arg{index}_callback_context->target, &arg{index}_callback_context->target_context')

	source_file.write(');\n\n')

	source_file.writelines([
		'\tif (status != ferr_ok) {\n',
		'\t\tgoto out;\n',
		'\t}\n',
	])

	for index, param in enumerate(type.parameters):
		if param.direction == Direction.IN:
			continue

		if isinstance(param.type, BasicType):
			source_file.writelines([
				f'\tstatus = spooky_invocation_set_{param.type.tag.name}(invocation, {index}, arg{index});\n',
				'\tif (status != ferr_ok) {\n',
				'\t\tgoto out;\n',
				'\t}\n',
			])
			if type_is_consumed(param.type.tag):
				# consumed upon successfully setting it
				source_file.write(f'\targ{index} = NULL;\n')
		elif isinstance(param.type, StructureType):
			source_file.writelines([
				f'\tstatus = spooky_invocation_set_structure(invocation, {index}, &arg{index});\n',
				'\tif (status != ferr_ok) {\n',
				'\t\tgoto out;\n',
				'\t}\n',
			])
		elif isinstance(param.type, FunctionType):
			source_file.writelines([
				f'\tstatus = spooky_invocation_set_function(invocation, {index}, _spookygen_callback_handler_{param.type_id}, arg{index}_callback_context);\n',
				'\tif (status != ferr_ok) {\n',
				'\t\tgoto out;\n',
				'\t}\n',
				'\targ{index}_callback_context = NULL;\n',
			])

	source_file.writelines([
		'\tstatus = spooky_invocation_complete(invocation);\n',
		'\tif (status != ferr_ok) {\n',
		'\t\tgoto out;\n',
		'\t}\n',
		'\n',
		'out:\n',
		'\tif (status != ferr_ok) {\n',
		'\t\tLIBSPOOKY_WUR_IGNORE(spooky_invocation_abort(invocation));\n',
		'\t}\n',
		'\n',
		'\tspooky_release(invocation);\n',
	])

	for index, param in enumerate(type.parameters):
		if param.direction == Direction.IN:
			#if isinstance(param.type, FunctionType):
			#	source_file.writelines([
			#		'\n',
			#		f'\tif (arg{index}_invocation) {{\n',
			#		f'\t\tspooky_release(arg{index}_invocation);\n',
			#		'\t}\n',
			#		'\n',
			#	])
			pass
		else:
			if isinstance(param.type, BasicType):
				if type_is_refcounted(param.type.tag):
					source_file.writelines([
						f'\tif (arg{index}) {{\n',
						f'\t\t{"sys" if type_is_libsys(param.type.tag) else "spooky"}_release(arg{index});\n',
						'\t}\n',
					])
			elif isinstance(param.type, StructureType):
				source_file.write(f'\tspooky_release_object_with_type(&arg{index}, _spookygen_types[{param.type_id}]);\n')
			elif isinstance(param.type, FunctionType):
				source_file.writelines([
					'\n',
					f'\tif (arg{index}_callback_context) {{\n',
					'\t\t// TODO: cleanup target context somehow\n',
					f'\t\tLIBSPOOKY_WUR_IGNORE(sys_mempool_free(arg{index}_callback_context));\n',
					'\t}\n',
					'\n',
				])

	source_file.writelines([
		'};\n\n',
	])

# i.e. for output callback implementations
# invoked directly by users
def write_outgoing_function_wrapper(name: str, type: FunctionType, target_name: str | None, is_root_interface: bool, raw_name: str | None):
	if name in written_names:
		return

	written_names.add(name)

	params = copy.deepcopy(type.parameters)

	for index, param in enumerate(params):
		param.name = f'arg{index}'
		write_function_type(param.type)

	for index, param in enumerate(params):
		if isinstance(param.type, FunctionType):
			if param.direction == Direction.IN:
				write_incoming_function_wrapper(f'_spookygen_callback_handler_{param.type_id}', param.type, None, False, None)
			else:
				write_outgoing_function_wrapper(f'_spookygen_callback_{param.type_id}', param.type, None, False, None)

	source_file.write(f'static ferr_t {name}(void* context')
	write_parameters(source_file, '', params, False, True)
	source_file.write(') {\n')
	source_file.write('\tferr_t status = ferr_ok;\n')
	source_file.write('\tspooky_invocation_t* invocation = context;\n')

	source_file.writelines([
		'\n',
		'\t_spookygen_ensure_init();\n',
		'\n',
	])

	if target_name == None:
		# this is a callback;
		# the context is the invocation to use
		pass
	else:
		# this is a client-side wrapper for an interface method.
		# the context is either an optional invocation to use or a proxy.
		# if it's not an invocation, we have to create the invocation ourselves.

		source_file.writelines([
			'\tif (invocation && spooky_object_class(invocation) != spooky_object_class_invocation()) {\n',
			'\t\tinvocation = NULL;\n',
			'\t}\n',
			'\n',
		])

		assert raw_name != None

		if not is_root_interface:
			source_file.writelines([
				f'\tstatus = spooky_invocation_create_proxy({json.dumps(raw_name)}, {len(raw_name)}, _spookygen_types[{type_to_id[type]}], context, &invocation);\n',
				'\tif (status != ferr_ok) {\n',
				'\t\tgoto out;\n',
				'\t}\n',
				'\n',
			])
		else:
			source_file.writelines([
				'\tif (!invocation) {\n',
				'\t\teve_channel_t* channel = NULL;\n',
				'\t\tsys_mutex_lock(&_spookygen_client_mutex);\n',
				'\t\tif (!_spookygen_channel) {\n',
			])

			if default_server_name == None:
				source_file.write('\t\t\tsys_abort();\n')
			else:
				source_file.writelines([
					f'\t\t\tstatus = {root_interface_name}_init_explicit_locked({json.dumps(default_server_name)}, {len(default_server_name)}, sys_channel_realm_{default_realm.name.lower()}, eve_loop_get_main());\n',
					'\t\t\tif (status != ferr_ok) {\n',
					'\t\t\t\tgoto out;\n',
					'\t\t\t}\n',
				])

			source_file.writelines([
				'\t\t}\n',
				'\t\tchannel = _spookygen_channel;\n',
				'\t\tstatus = eve_retain(channel);\n',
				'\t\tif (status != ferr_ok) {\n',
				'\t\t\tgoto out;\n',
				'\t\t}\n',
				'\t\tsys_mutex_unlock(&_spookygen_client_mutex);\n',
				f'\t\tstatus = spooky_invocation_create({json.dumps(raw_name)}, {len(raw_name)}, _spookygen_types[{type_to_id[type]}], channel, &invocation);\n',
				'\t\teve_release(channel);\n',
				'\t\tif (status != ferr_ok) {\n',
				'\t\t\tgoto out;\n',
				'\t\t}\n',
				'\t}\n',
				'\n',
			])

	for index, param in enumerate(params):
		if param.direction == Direction.OUT:
			continue

		if isinstance(param.type, BasicType):
			source_file.writelines([
				f'\tstatus = spooky_invocation_set_{param.type.tag.name}(invocation, {index}, arg{index});\n',
				'\tif (status != ferr_ok) {\n',
				'\t\tgoto out;\n',
				'\t}\n',
			])
			if type_is_consumed(param.type.tag):
				# consumed upon successfully setting it
				source_file.write(f'\targ{index} = NULL;\n')
		elif isinstance(param.type, StructureType):
			source_file.writelines([
				f'\tstatus = spooky_invocation_set_structure(invocation, {index}, arg{index});\n',
				'\tif (status != ferr_ok) {\n',
				'\t\tgoto out;\n',
				'\t}\n',
			])
		elif isinstance(param.type, FunctionType):
			source_file.writelines([
				f'\tstruct _spookygen_callback_context* arg{index}_callback_context = NULL;\n',
				f'\tstatus = sys_mempool_allocate(sizeof(*arg{index}_callback_context), NULL, (void*)&arg{index}_callback_context);\n',
				'\tif (status != ferr_ok) {\n',
				'\t\tgoto out;\n',
				'\t}\n',
				f'\targ{index}_callback_context->target = arg{index};\n',
				f'\targ{index}_callback_context->target_context = _context_arg{index};\n',
				f'\tstatus = spooky_invocation_set_function(invocation, {index}, _spookygen_callback_handler_{param.type_id}, arg{index}_callback_context));\n',
				'\tif (status != ferr_ok) {\n',
				'\t\tgoto out;\n',
				'\t}\n',
				# the callback context is stored in the invocation and will be passed to the function upon cleanup of the invocation
				f'\targ{index}_callback_context = NULL;\n'
			])

	source_file.writelines([
		'\tstatus = spooky_invocation_execute_sync(invocation);\n',
		'\tif (status != ferr_ok) {\n',
		'\t\tgoto out;\n',
		'\t}\n',
	])

	for index, param in enumerate(params):
		if param.direction == Direction.IN:
			continue

		if isinstance(param.type, BasicType):
			retain_arg = ", true" if type_is_refcounted(param.type.tag) else ""
			source_file.writelines([
				f'\tbool arg{index}_should_cleanup_on_fail = false;\n',
				f'\tstatus = spooky_invocation_get_{param.type.tag.name}(invocation, {index}{retain_arg}, arg{index});\n',
				'\tif (status != ferr_ok) {\n',
				'\t\tgoto out;\n',
				'\t}\n',
				f'\targ{index}_should_cleanup_on_fail = true;\n',
			])
		elif isinstance(param.type, StructureType):
			source_file.writelines([
				f'\tsize_t arg{index}_size = sizeof(*arg{index});\n',
				f'\tbool arg{index}_should_cleanup_on_fail = false;\n',
				f'\tstatus = spooky_invocation_get_structure(invocation, {index}, true, arg{index}, &arg{index}_size);\n',
				'\tif (status != ferr_ok) {\n',
				'\t\tgoto out;\n',
				'\t}\n',
				f'\targ{index}_should_cleanup_on_fail = true;\n',
			])
		elif isinstance(param.type, FunctionType):
			source_file.writelines([
				f'\tspooky_invocation_t* arg{index}_invocation = NULL;\n',
				f'\tstatus = spooky_invocation_get_invocation(invocation, {index}, &arg{index}_invocation);\n',
				'\tif (status != ferr_ok) {\n',
				'\t\tgoto out;\n',
				'\t}\n',
				f'\t*arg{index} = _spookygen_callback_{param.type_id};\n',
				f'\t*_context_arg{index} = arg{index}_invocation;\n',
			])

	source_file.writelines([
		'out:\n',
		'\tif (invocation) {\n',
		'\t\tspooky_release(invocation);\n',
		'\t}\n',
		'\n',
		'\tif (status != ferr_ok) {\n',
	])

	for index, param in enumerate(params):
		if param.direction == Direction.OUT:
			if isinstance(param.type, BasicType):
				if type_is_refcounted(param.type.tag):
					source_file.writelines([
						f'\t\tif (arg{index} && arg{index}_should_cleanup_on_fail) {{\n',
						f'\t\t\t{"sys" if type_is_libsys(param.type.tag) else "spooky"}_release(*arg{index});\n',
						'\t\t}\n',
					])
			elif isinstance(param.type, StructureType):
				source_file.writelines([
					f'\t\tif (arg{index} && arg{index}_should_cleanup_on_fail) {{\n',
					f'\t\t\tspooky_release_object_with_type(arg{index}, _spookygen_types[{param.type_id}]);\n',
					'\t\t}\n',
				])
			elif isinstance(param.type, FunctionType):
				source_file.writelines([
					f'\t\tif (arg{index}_invocation) {{\n',
					f'\t\t\tspooky_release(arg{index}_invocation);\n',
					'\t\t}\n',
				])
		else:
			if isinstance(param.type, FunctionType):
				source_file.writelines([
					f'\t\tif (arg{index}_callback_context) {{\n',
					f'\t\t\tLIBSPOOKY_WUR_IGNORE(sys_mempool_free(arg{index}_callback_context));\n',
					'\t\t}\n',
				])

	source_file.writelines([
		'\t}\n',
		'\n',
		'\treturn status;\n',
		'};\n\n',
	])

for interface in interfaces:
	for function in interface.functions:
		type = cast(FunctionType, id_to_type[function.type_id])

		is_root_interface = interface.name == root_interface_name

		if args.server or not is_root_interface:
			write_incoming_function_wrapper(f'_spookygen_impl_{interface.name}_{function.name}', type, f'{interface.name}_{function.name}_impl', interface.name == root_interface_name, function.name)

		if not args.server or not is_root_interface:
			write_outgoing_function_wrapper(f'_spookygen_internal_{interface.name}_{function.name}', type, f'{interface.name}_{function.name}', interface.name == root_interface_name, function.name)

			source_file.write(f'SPOOKYGEN_{root_interface_name}_STORAGE ferr_t {interface.name}_{function.name}(void* _spookygen_context')
			write_parameters(source_file, f'{interface.name}_{function.name}', function.parameters, False)
			source_file.write(') {\n')
			source_file.write(f'\treturn _spookygen_internal_{interface.name}_{function.name}(_spookygen_context')
			for param in function.parameters:
				if isinstance(param.type, BasicType):
					source_file.write(f', {param.name}')
				elif isinstance(param.type, StructureType):
					const_str = "const " if param.direction == Direction.IN else ""
					source_file.write(f', ({const_str}void*){param.name}')
				elif isinstance(param.type, FunctionType):
					source_file.write(f', (void*){param.name}, _context_{param.name}')
			source_file.write(');\n')
			source_file.write('};\n\n')

if args.server:
	root_interface = [x for x in interfaces if x.name == root_interface_name][0]

	header_file.write(f'spooky_interface_t* {root_interface_name}_interface(void);\n')
	if default_server_name != None:
		header_file.write(f'SPOOKYGEN_{root_interface_name}_STORAGE LIBSPOOKY_WUR ferr_t {root_interface_name}_serve(eve_loop_t* loop, eve_server_channel_t** out_server_channel);\n')
	header_file.write(f'SPOOKYGEN_{root_interface_name}_STORAGE LIBSPOOKY_WUR ferr_t {root_interface_name}_serve_explicit(const char* name, size_t name_length, sys_channel_realm_t realm, eve_loop_t* loop, eve_server_channel_t** out_server_channel);\n')

	source_file.writelines([
		'static void _spookygen_server_handler(void* context, eve_server_channel_t* server_channel, sys_channel_t* channel) {\n',
		'\tLIBSPOOKY_WUR_IGNORE(spooky_interface_adopt(_spookygen_interface, channel, eve_loop_get_current()));\n',
		'};\n\n',
	])

	if default_server_name != None:
		source_file.writelines([
			f'SPOOKYGEN_{root_interface_name}_STORAGE ferr_t {root_interface_name}_serve(eve_loop_t* loop, eve_server_channel_t** out_server_channel) {{\n',
			f'\treturn {root_interface_name}_serve_explicit({json.dumps(default_server_name)}, {len(default_server_name)}, sys_channel_realm_{default_realm.name.lower()}, loop, out_server_channel);\n',
			'};\n\n',
		])

	source_file.writelines([
		f'SPOOKYGEN_{root_interface_name}_STORAGE ferr_t {root_interface_name}_serve_explicit(const char* name, size_t name_length, sys_channel_realm_t realm, eve_loop_t* loop, eve_server_channel_t** out_server_channel) {{\n',
		'\tferr_t status = ferr_ok;\n',
		f'\tspooky_interface_entry_t entries[{len(root_interface.functions)}];\n',
		'\tsys_server_channel_t* sys_server_channel = NULL;\n',
		'\n',
		'\t_spookygen_ensure_init();\n',
		'\n',
		'\tsys_mutex_lock(&_spookygen_server_mutex);\n',
		'\n',
		'\tif (_spookygen_server_channel != NULL) {\n',
		'\t\tstatus = ferr_already_in_progress;\n',
		'\t\tgoto out;\n'
		'\t}\n',
		'\n',
	])

	idx = 0
	for function in root_interface.functions:
		source_file.writelines([
			f'\tentries[{idx}].name = {json.dumps(function.name)};\n',
			f'\tentries[{idx}].name_length = {len(function.name)};\n',
			f'\tentries[{idx}].function = _spookygen_types[{function.type_id}];\n',
			f'\tentries[{idx}].implementation = _spookygen_impl_{root_interface_name}_{function.name};\n',
			f'\tentries[{idx}].context = NULL;\n',
		])
		idx += 1

	source_file.writelines([
		'\n',
		f'\tstatus = spooky_interface_create(entries, {len(root_interface.functions)}, &_spookygen_interface);\n',
		'\tif (status != ferr_ok) {\n',
		'\t\tgoto out;\n'
		'\t}\n',
		'\n',
		'\tstatus = sys_server_channel_create_n(name, name_length, realm, &sys_server_channel);\n',
		'\tif (status != ferr_ok) {\n',
		'\t\tgoto out;\n'
		'\t}\n',
		'\n',
		'\tstatus = eve_server_channel_create(sys_server_channel, NULL, &_spookygen_server_channel);\n',
		'\tif (status != ferr_ok) {\n',
		'\t\tgoto out;\n'
		'\t}\n',
		'\n',
		'\tsys_release(sys_server_channel);\n',
		'\tsys_server_channel = NULL;\n',
		'\n',
		'\teve_server_channel_set_handler(_spookygen_server_channel, _spookygen_server_handler);\n',
		'\n',
		'\tstatus = eve_loop_add_item(loop, _spookygen_server_channel);\n',
		'\tif (status != ferr_ok) {\n',
		'\t\teve_release(_spookygen_server_channel);\n',
		'\t\t_spookygen_server_channel = NULL;\n',
		'\t\tgoto out;\n'
		'\t}\n',
		'\n',
		'\tif (out_server_channel) {\n',
		'\t\t// this cannot fail\n',
		'\t\tsys_abort_status_log(eve_retain(_spookygen_server_channel));\n',
		'\t\t*out_server_channel = _spookygen_server_channel;\n',
		'\t}\n',
		'\n',
		'out:\n',
		'\tsys_mutex_unlock(&_spookygen_server_mutex);\n',
		'\n',
		'\tif (status != ferr_ok) {\n',
		'\t\tif (sys_server_channel) {\n',
		'\t\t\tsys_release(sys_server_channel);\n',
		'\t\t}\n',
		'\t}\n',
		'\n',
		'\treturn status;\n'
		'};\n\n',
	])
else:
	if default_server_name != None:
		header_file.write(f'SPOOKYGEN_{root_interface_name}_STORAGE LIBSPOOKY_WUR ferr_t {root_interface_name}_init(eve_loop_t* loop);\n')
	header_file.write(f'SPOOKYGEN_{root_interface_name}_STORAGE LIBSPOOKY_WUR ferr_t {root_interface_name}_init_explicit(const char* name, size_t name_length, sys_channel_realm_t realm, eve_loop_t* loop);\n')

	if default_server_name != None:
		source_file.writelines([
			f'SPOOKYGEN_{root_interface_name}_STORAGE ferr_t {root_interface_name}_init(eve_loop_t* loop) {{\n',
			f'\treturn {root_interface_name}_init_explicit({json.dumps(default_server_name)}, {len(default_server_name)}, sys_channel_realm_{default_realm.name.lower()}, loop);\n',
			'};\n\n',
		])

	source_file.writelines([
		'static void _spookygen_client_message_handler(void* context, eve_channel_t* channel, sys_channel_message_t* message) {\n',
		'\t// just discard the message\n',
		'\tsys_release(message);\n',
		'};\n',
		'\n',
		'static void _spookygen_client_peer_close_handler(void* context, eve_channel_t* channel) {\n',
		'\tsys_mutex_lock(&_spookygen_client_mutex);\n',
		'\t// this shouldn\'t fail\n',
		'\tsys_abort_status_log(eve_loop_remove_item(eve_loop_get_current(), channel));\n',
		'\teve_release(_spookygen_channel);\n',
		'\t_spookygen_channel = NULL;\n',
		'\tsys_mutex_unlock(&_spookygen_client_mutex);\n',
		'};\n',
		'\n',
		'static void _spookygen_message_send_error_handler(void* context, eve_channel_t* channel, sys_channel_message_t* message, ferr_t error) {\n',
		'\t// just discard the message\n',
		'\t// TODO: report the error somehow\n',
		'\tsys_release(message);\n',
		'};\n\n',
	])

	source_file.writelines([
		f'static ferr_t {root_interface_name}_init_explicit_locked(const char* name, size_t name_length, sys_channel_realm_t realm, eve_loop_t* loop) {{\n',
		'\tferr_t status = ferr_ok;\n',
		'\tsys_channel_t* sys_channel = NULL;\n',
		'\n',
		'\t_spookygen_ensure_init();\n',
		'\n',
		'\tif (_spookygen_channel != NULL) {\n',
		'\t\tstatus = ferr_already_in_progress;\n',
		'\t\tgoto out;\n'
		'\t}\n',
		'\n',
		'\tstatus = sys_channel_connect_n(name, name_length, realm, /* TODO: make this optional */ sys_channel_connect_flag_recursive_realm, &sys_channel);',
		'\tif (status != ferr_ok) {\n',
		'\t\tgoto out;\n'
		'\t}\n',
		'\n',
		'\tstatus = eve_channel_create(sys_channel, NULL, &_spookygen_channel);\n',
		'\tif (status != ferr_ok) {\n',
		'\t\tgoto out;\n'
		'\t}\n',
		'\n',
		'\tsys_release(sys_channel);\n',
		'\tsys_channel = NULL;\n',
		'\n',
		'\teve_channel_set_message_handler(_spookygen_channel, _spookygen_client_message_handler);\n',
		'\teve_channel_set_peer_close_handler(_spookygen_channel, _spookygen_client_peer_close_handler);\n',
		'\teve_channel_set_message_send_error_handler(_spookygen_channel, _spookygen_message_send_error_handler);\n',
		'\n',
		'\tstatus = eve_loop_add_item(loop, _spookygen_channel);\n',
		'\tif (status != ferr_ok) {\n',
		'\t\teve_release(_spookygen_channel);\n',
		'\t\t_spookygen_channel = NULL;\n',
		'\t\tgoto out;\n'
		'\t}\n',
		'\n',
		'out:\n',
		'\tif (status != ferr_ok) {\n',
		'\t\tif (sys_channel) {\n',
		'\t\t\tsys_release(sys_channel);\n',
		'\t\t}\n',
		'\t}\n',
		'\n',
		'\treturn status;\n',
		'};\n\n',
	])

	source_file.writelines([
		f'SPOOKYGEN_{root_interface_name}_STORAGE ferr_t {root_interface_name}_init_explicit(const char* name, size_t name_length, sys_channel_realm_t realm, eve_loop_t* loop) {{\n',
		f'\tsys_mutex_lock(&_spookygen_client_mutex);\n',
		f'\tferr_t status = {root_interface_name}_init_explicit_locked(name, name_length, realm, loop);\n',
		'\tsys_mutex_unlock(&_spookygen_client_mutex);\n',
		'\treturn status;\n',
		'};\n\n',
	])

for interface in interfaces:
	if interface.name == root_interface_name:
		continue

	header_file.write(f'\nSPOOKYGEN_{root_interface_name}_STORAGE LIBSPOOKY_WUR ferr_t {interface.name}_create_proxy(const {interface.name}_proxy_info_t* info, spooky_proxy_t** out_proxy);\n')

	source_file.writelines([
		f'static void {interface.name}_proxy_destructor(void* context) {{\n',
		f'\t{interface.name}_proxy_info_t* info = context;\n',
		'\tif (info->destructor) {\n',
		'\t\tinfo->destructor(info->context);\n',
		'\t}\n',
		'\tLIBSPOOKY_WUR_IGNORE(sys_mempool_free(context));\n',
		'};\n\n',
	])

	source_file.writelines([
		f'SPOOKYGEN_{root_interface_name}_STORAGE LIBSPOOKY_WUR ferr_t {interface.name}_create_proxy(const {interface.name}_proxy_info_t* info, spooky_proxy_t** out_proxy) {{\n',
		'\tferr_t status = ferr_ok;\n',
		f'\tspooky_proxy_interface_t* proxy_interface = NULL;\n',
		f'\tspooky_proxy_interface_entry_t entries[{len(interface.functions)}];\n',
		f'\t{interface.name}_proxy_info_t* copy = NULL;\n',
		'\n',
		'\t_spookygen_ensure_init();\n',
		'\n',
		'\tstatus = sys_mempool_allocate(sizeof(*copy), NULL, (void*)&copy);\n',
		'\tif (status != ferr_ok) {\n',
		'\t\tgoto out;\n',
		'\t}\n',
		'\n',
		'\tsimple_memcpy(copy, info, sizeof(*copy));\n',
		'\n',
	])

	idx = 0
	for function in interface.functions:
		source_file.writelines([
			f'\tentries[{idx}].name = {json.dumps(function.name)};\n',
			f'\tentries[{idx}].name_length = {len(function.name)};\n',
			f'\tentries[{idx}].function = _spookygen_types[{function.type_id}];\n',
			f'\tentries[{idx}].implementation = _spookygen_impl_{interface.name}_{function.name};\n',
		])
		idx += 1

	source_file.writelines([
		'\n',
		f'\tstatus = spooky_proxy_interface_create(entries, {len(interface.functions)}, &proxy_interface);\n',
		'\tif (status != ferr_ok) {\n',
		'\t\tgoto out;\n',
		'\t}\n',
		'\n',
		f'\tstatus = spooky_proxy_create(proxy_interface, copy, {interface.name}_proxy_destructor, out_proxy);\n',
		'\tif (status != ferr_ok) {\n',
		'\t\tgoto out;\n',
		'\t}\n',
		'\n',
		'out:\n',
		'\tif (status == ferr_ok) {\n',
		'\t\t// nothing for now\n',
		'\t} else {\n',
		'\t\tif (copy) {\n',
		'\t\t\tLIBSPOOKY_WUR_IGNORE(sys_mempool_free(copy));\n',
		'\t\t}\n',
		'\t}\n',
		'\n',
		'\tif (proxy_interface) {\n',
		'\t\tspooky_release(proxy_interface);\n',
		'\t}\n',
		'\n',
		'\treturn status;\n',
		'};\n\n',
	])

source_file.close()
header_file.close()
