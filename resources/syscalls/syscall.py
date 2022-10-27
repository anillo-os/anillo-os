#!/usr/bin/env python3

from typing import Dict, List, Tuple

static_syscall_type_to_c_type: Dict[str, str] = {
	'u8': 'uint8_t',
	'u16': 'uint16_t',
	'u32': 'uint32_t',
	'u64': 'uint64_t',
	'i8': 'int8_t',
	'i16': 'int16_t',
	'i32': 'int32_t',
	'i64': 'int64_t',
	'string': 'char const*',
	'*': 'void*',
	'*c': 'void const*',
	'mut_string': 'char*',
	'char': 'char',
}

def syscall_type_to_c_type(syscall_type: str, kernel: bool) -> str:
	syscall_type = syscall_type.strip()

	if syscall_type in static_syscall_type_to_c_type:
		return static_syscall_type_to_c_type[syscall_type]

	if (syscall_type.startswith('*[') or syscall_type.startswith('*c[')) and syscall_type.endswith(']'):
		is_const = syscall_type[1] == 'c'
		pointed_type = syscall_type[3:-1] if is_const else syscall_type[2:-1]
		return f'{syscall_type_to_c_type(pointed_type, kernel)}{" const" if is_const else ""}*'

	if syscall_type.startswith('e:') or syscall_type.startswith('s:'):
		return f'{"fsyscall" if kernel else "libsyscall"}_{syscall_type[2:]}_t'

	if syscall_type.startswith('!'):
		return syscall_type[1:]

	raise ValueError(f'Invalid/unparsable syscall module type: {syscall_type}')

class Parameter:
	"""
	An object that describes a syscall parameter.

	:ivar type: A typestring describing the parameter's type.
	            This should be 'u8', 'u16', 'u32', 'u64', 'i8', 'i16', 'i32', 'i64', '*', or '*c'.
	            The 'u'-prefixed types are unsigned integers and the 'i'-prefixed types are signed integers.
	            The '*' type is for pointers and the '*c' type is for constant pointers (i.e. ones whose values cannot be modified).
	:ivar name: A name for the parameter.
	"""

	def __init__(self, type: str, name: str) -> None:
		"""
		Initializes a Parameter object.

		:param type: A typestring describing the parameter's type. For the exact format, see the class documentation.
		:param name: A name for the parameter.
		"""
		self.type = type
		self.name = name

	def __str__(self) -> str:
		return f'{self.name}: {self.type} ({syscall_type_to_c_type(self.type, False)})'

	def __repr__(self) -> str:
		return self.__str__()

class Syscall:
	"""
	An object that describes a syscall.

	:ivar number: The syscall's number.
	:ivar name: The syscall's name.
	:ivar parameters: The parameters that the syscall expects.
	"""

	# NOTE: this function depends on on Python 3.6's ordering of kwargs to work properly
	#       we're using type annotations which were introduced in 3.8, so this shouldn't be a problem for us,
	#       but still, be aware of this fact.
	def __init__(self, _number: int, _name: str, **_parameters: str) -> None:
		"""
		Initializes a new Syscall object with the given information.

		:param _number: The syscall's number.
		:param _name: The syscall's name.
		:param parameters: The parameters that the syscall expects.
		                   This is a kwarg-type argument; each key is a parameter name
		                   and each value is the parameter's type.

		This function depends on on Python 3.6's ordering of kwargs to work properly.
		We're using type annotations which were introduced in 3.8, so this shouldn't be a problem for us,
		but still, you should be aware of this fact.
		"""
		self.number = _number
		self.name = _name
		self.parameters = [Parameter(_parameters[key], key) for key in _parameters]

	def __str__(self) -> str:
		return f'Syscall "{self.name}" ({self.number}) with parameters {self.parameters}'

	def __repr__(self) -> str:
		return self.__str__()

class Member:
	def __init__(self, name: str, type: str) -> None:
		self.name = name
		self.type = type

	def __str__(self) -> str:
		return f'{self.name}: {self.type} ({syscall_type_to_c_type(self.type, False)})'

	def __repr__(self) -> str:
		return self.__str__()

class Structure:
	"""
	An object that describes a data structure.
	"""

	def __init__(self, name: str, members: List[Tuple[str, str]]) -> None:
		self.name = name
		self.members = [Member(name, type) for [name, type] in members]

	def __str__(self) -> str:
		members_string = ''.join([f'\t{member}\n' for member in self.members])
		if len(members_string) != 0:
			members_string = '\n' + members_string
		return f'Structure "{self.name}" {{{members_string}}}'

	def __repr__(self) -> str:
		return self.__str__()

class EnumValue:
	def __init__(self, name: str, value: str) -> None:
		self.name = name
		self.value = value

	def __str__(self) -> str:
		return f'{self.name}: {self.value}'

	def __repr__(self) -> str:
		return self.__str__()

class Enum:
	"""
	An object that represents an enumeration.
	"""

	def __init__(self, name: str, type: str, values: List[Tuple[str, str]], prefix: str = None) -> None:
		self.name = name
		self.type = type
		self.prefix = self.name if prefix is None else prefix
		self.values = [EnumValue(key, value) for [key, value] in values]

	def __str__(self) -> str:
		values_string = ''.join([f'\t{value}\n' for value in self.values])
		if len(values_string) != 0:
			values_string = '\n' + values_string
		return f'Enum "{self.name}": {self.type} ({syscall_type_to_c_type(self.type, False)}) {{{values_string}}}'

class SyscallList(List[Syscall]):
	def sort_syscalls(self) -> None:
		self.sort(key=lambda x: x.number, reverse=False)

	def validate_syscalls(self) -> None:
		for syscall in self:
			if len(syscall.parameters) > 6:
				raise RuntimeError(f"Too many parameters for syscall (maximum of 6 allowed): {syscall}")

	def add_syscall(self, _name: str, **_parameters: str):
		self.extend([
			Syscall(len(self) + 1, _name, **_parameters),
		])
		return self

syscalls = SyscallList()
structures: List[Structure] = []
enums: List[Enum] = []
