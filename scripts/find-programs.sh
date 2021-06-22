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
	# look for clang first; we prefer it and it already cross compiles by default
	CC="clang"
elif command-exists "${ARCH}-linux-gnu-gcc"; then
	# otherwise, try GCC
	# TODO: GCC has different target names that we should try, not just this one
	CC="${ARCH}-linux-gnu-gcc"
else
	die-red "No compiler for ${ARCH} found"
fi

# CC can be used as AS
AS="${CC}"

# *don't* try lld (LLVM ld); it generates broken output (or at least i don't know the correct flags to use for proper output)
if command-exists "${ARCH}-linux-gnu-ld"; then
	# TODO: like above, GNU ld has different target names we should try that we currently don't
	LD="${ARCH}-linux-gnu-ld"
else
	die-red "No linker for ${ARCH} found"
fi

if command-exists "${ARCH}-linux-gnu-objcopy"; then
	# TODO: ditto from before
	OBJCOPY="${ARCH}-linux-gnu-objcopy"
else
	die-red "No objdump for ${ARCH} found"
fi

if [ "${VERBOSE}" == "true" ]; then
	echo "Using CC=${CC}"
	echo "Using LD=${LD}"
	echo "Using AS=${AS}"
	echo "Using OBJCOPY=${OBJCOPY}"
fi
