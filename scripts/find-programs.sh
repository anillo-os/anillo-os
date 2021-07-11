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
	CC="clang"
else
	die-red "No compiler for ${ARCH} found"
fi

if command-exists "lld"; then
	LD="lld"
else
	die-red "No linker for ${ARCH} found"
fi

if [ "${VERBOSE}" == "true" ]; then
	echo "Using CC=${CC}"
	echo "Using LD=${LD}"
fi
