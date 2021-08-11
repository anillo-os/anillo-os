#!/bin/bash

_ANILLO_INCLUDED_FIND_PROGRAMS=1

if [ -z ${_ANILLO_INCLUDED_UTIL+x} ]; then
	# since we don't have util.sh included, we can't use `die`
	echo "Include util.sh before including find-programs.sh" >&2
	exit 1
fi

if [ -z ${ARCH+x} ]; then
	die-red "ARCH must be set before including find-programs.sh"
fi

if command-exists clang; then
	CC="$(command-path clang)"
else
	die-red "No compiler for ${ARCH} found"
fi

if command-exists "ld.lld"; then
	LD="$(command-path ld.lld)"
else
	die-red "No linker for ${ARCH} found"
fi

if command-exists llvm-ar; then
	AR="$(command-path llvm-ar)"
else
	die-red "No archiver for ${ARCH} found"
fi

if command-exists llvm-nm; then
	NM="$(command-path llvm-nm)"
else
	die-red "No nm for ${ARCH} found"
fi

if command-exists llvm-ranlib; then
	RANLIB="$(command-path llvm-ranlib)"
else
	die-red "No ranlib for ${ARCH} found"
fi

if command-exists llvm-config; then
	LLVM_CONFIG="$(command-path llvm-config)"
else
	die-red "No llvm-config for ${ARCH} found"
fi

if [ "${VERBOSE}" == "true" ]; then
	echo "Using CC=${CC}"
	echo "Using LD=${LD}"
fi
