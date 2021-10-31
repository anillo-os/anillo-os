#!/usr/bin/env python3

from typing import Dict, List, Tuple

syscall_type_to_c_type: Dict[str, str] = {
	 'u8': 'uint8_t',
	'u16': 'uint16_t',
	'u32': 'uint32_t',
	'u64': 'uint64_t',
	 'i8': 'int8_t',
	'i16': 'int16_t',
	'i32': 'int32_t',
	'i64': 'int64_t',
	  '*': 'void*',
	 '*c': 'const void*',
}

class SyscallParameter:
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
		Initializes a SyscallParameter object.

		:param type: A typestring describing the parameter's type. For the exact format, see the class documentation.
		:param name: A name for the parameter.
		"""
		self.type = type
		self.name = name

	def __str__(self) -> str:
		return f'{self.name}: {self.type} ({syscall_type_to_c_type[self.type]})'

	def __repr__(self) -> str:
		return self.__str__()

class Syscall:
	"""
	An object that describes a syscall.

	:ivar number: The syscall's number.
	:ivar name: The syscall's name.
	:ivar parameters: The parameters that the syscall expects.
	"""

	def __init__(self, number: int, name: str, parameters: List[SyscallParameter]) -> None:
		"""
		Initializes a new Syscall object with the given information.

		:param number: The syscall's number.
		:param name: The syscall's name.
		:param parameters: The parameters that the syscall expects.
		"""
		self.number = number
		self.name = name
		self.parameters = parameters

	def __str__(self) -> str:
		return f'Syscall "{self.name}" ({self.number}) with parameters {self.parameters}'

	def __repr__(self) -> str:
		return self.__str__()

def sort_syscalls() -> None:
	syscalls.sort(key=lambda x: x.number, reverse=False)

def validate_syscalls() -> None:
	for syscall in syscalls:
		if len(syscall.parameters) > 6:
			raise RuntimeError(f"Too many parameters for syscall (maximum of 6 allowed): {syscall}")

syscalls: List[Syscall] = []
