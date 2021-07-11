#!/bin/bash

_ANILLO_INCLUDED_UTIL=1

SOURCE_ROOT="$(dirname "${BASH_SOURCE}")/.."
KERNEL_SOURCE_ROOT="${SOURCE_ROOT}/kernel"

SUPPORTED_ARCHS=(
	x86_64
	aarch64
)

#
# text coloring utility functions
#

color-red() {
	echo "$(tput setaf 1)""$@""$(tput sgr0)"
}

color-green() {
	echo "$(tput setaf 2)""$@""$(tput sgr0)"
}

color-yellow() {
	echo "$(tput setaf 3)""$@""$(tput sgr0)"
}

color-blue() {
	echo "$(tput setaf 4)""$@""$(tput sgr0)"
}

#
# silently returns true if the command exists and false otherwise
#
command-exists() {
	command -v "$1" >/dev/null 2>&1
}

#
# echo to stderr
#
errcho() {
	echo "$@" >&2
}

#
# print the arguments to stderr and exit with a failure status
#
die() {
	errcho "$@"
	exit 1
}

#
# print the argument to stderr, colored in red, and exit with a failure status
#
die-red() {
	die "$(color-red "$@")"
}

#
# die with a message indicating the last command failed
#
command-failed() {
	die-red "Command failed"
}

#
# round argument 1 up to a multiple of argument 2
#
round-up() {
	echo "$(( (($1 + ($2 - 1)) / $2) * $2 ))"
}

#
# round argument 1 down to a multiple of argument 2
#
round-down() {
	echo "$(( (($1) / $2) * $2 ))"
}

#
# run a command with some arguments
#
# when running verbosely, automatically prints the command before running it
#
run-command() {
	if [ "${VERBOSE}" == "true" ]; then
		echo "$@"
	fi

	"$@"
}

#
# determine whether file 1 is newer than file 2
#
# this will also return true if file 2 does not exist.
# likewise, it will also return false if file 1 does not exist.
# if neither exists, it will return false.
#
file-is-newer() {
	if [ ! -f "$1" ]; then
		return 1
	fi
	if [ ! -f "$2" ]; then
		return 0
	fi
	[ "$1" -nt "$2" ]
}

#
# variable setup
#

if [ -z ${ARCH+x} ]; then
	ARCH="$(uname -m)"
fi

if [ -z ${BUILD_ROOT+x} ]; then
	BUILD_ROOT="${SOURCE_ROOT}/build"
fi

if [ -z ${BUILD_DIR+x} ]; then
	BUILD_DIR="${BUILD_ROOT}/${ARCH}"
else
	color-yellow "Build directory has been manually set to \"${BUILD_DIR}\" using the \`BUILD_DIR\` variable. This is not recommended; use the \`BUILD_ROOT\` variable instead."
fi

for ARG in "$@"; do
	if [ "$ARG" == "-v" ] || [ "$ARG" == "--verbose" ]; then
		VERBOSE=true
		break
	fi
done

if [[ ! " ${SUPPORTED_ARCHS[@]} " =~ " ${ARCH} " ]]; then
	die-red "Unsupported architecture: ${ARCH}"
fi
