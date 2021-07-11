#!/bin/bash

SCRIPT_PATH="$(cd "$(dirname "$0")" >/dev/null 2>&1; pwd -P)"
SOURCE_ROOT="${SCRIPT_PATH}/../../../.."
KERNEL_SOURCE_ROOT="${SOURCE_ROOT}/kernel"

source "${SOURCE_ROOT}/scripts/util.sh"

source "${SOURCE_ROOT}/scripts/find-programs.sh"

CURRENT_BUILD_DIR="${BUILD_DIR}/kernel/src/bootstrap/uefi"

#
# flags
#
CFLAGS=(
	"-I${KERNEL_SOURCE_ROOT}/include"
	#-fpic
	-ffreestanding
	-fno-stack-protector
	-fno-stack-check
	-fshort-wchar
	-mno-red-zone
	-c
	-target ${ARCH}-unknown-windows
)
LDFLAGS=(
	"-fuse-ld=${LD}"
	#-Wl,-flavor,link
	-target x86_64-unknown-windows
	-nostdlib
	-Wl,-entry:efi_main
	-Wl,-subsystem:efi_application
)

OBJECTS=()

#
# helper functions
#

compile() {
	if [ "x${ANILLO_GENERATING_COMPILE_COMMANDS}" == "x1" ]; then
		add-compile-command "${KERNEL_SOURCE_ROOT}/${1}" "$(echo "${CC}" "${CFLAGS[@]}" -c "${KERNEL_SOURCE_ROOT}/${1}" -o "${CURRENT_BUILD_DIR}/${1}.o")"
	else
		echo "$(color-blue CC) ${1}.o"
		mkdir -p "${CURRENT_BUILD_DIR}/$(dirname "${1}")" || command-failed
		run-command "${CC}" "${CFLAGS[@]}" -c "${KERNEL_SOURCE_ROOT}/${1}" -o "${CURRENT_BUILD_DIR}/${1}.o" || command-failed
	fi
}

#
# this is where the compilation actually starts
#

mkdir -p "${CURRENT_BUILD_DIR}"

pushd "${CURRENT_BUILD_DIR}" >/dev/null

compile "src/bootstrap/uefi/main.c"
OBJECTS+=("${CURRENT_BUILD_DIR}/src/bootstrap/uefi/main.c.o")

compile "src/bootstrap/uefi/wrappers.c"
OBJECTS+=("${CURRENT_BUILD_DIR}/src/bootstrap/uefi/wrappers.c.o")

if ! [ "x${ANILLO_GENERATING_COMPILE_COMMANDS}" == "x1" ]; then
	echo "$(color-blue CC-LD) ferro-bootstrap.efi"
	run-command "${CC}" "${LDFLAGS[@]}" "${OBJECTS[@]}" -o "${CURRENT_BUILD_DIR}/ferro-bootstrap.efi" || command-failed
fi

popd >/dev/null
