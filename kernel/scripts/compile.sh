#!/bin/bash

SCRIPT_PATH="$(cd "$(dirname "$0")" >/dev/null 2>&1; pwd -P)"
KERNEL_SOURCE_ROOT="${SCRIPT_PATH}/.."
ARCH=x86_64
CC=clang
LD=ld
BUILD_DIR="${KERNEL_SOURCE_ROOT}/build"
CFLAGS=(
	"-I${KERNEL_SOURCE_ROOT}/include"
	"-I${BUILD_DIR}/include"
	-fno-asynchronous-unwind-tables
	-ffreestanding
	-fno-stack-protector
	-fno-stack-check
	-fno-omit-frame-pointer
	-mno-red-zone
	-mcmodel=large
	-g
)
LDFLAGS=(
	"-T${KERNEL_SOURCE_ROOT}/scripts/kernel.lds"
)
SOURCES=(
	src/core/entry.c
	src/core/console.c
	src/core/framebuffer.c
	src/libk/libk.c
)

die() {
	echo "Command failed"
	exit 1
}

compile() {
	echo "CC ${1}.o"
	mkdir -p "${BUILD_DIR}/$(dirname "${1}")"
	"${CC}" "${CFLAGS[@]}" -c "${KERNEL_SOURCE_ROOT}/${1}" -o "${BUILD_DIR}/${1}.o" || die
}

OBJECTS=()

for SOURCE in "${SOURCES[@]}"; do
	compile "${SOURCE}" || die
	OBJECTS+=("${BUILD_DIR}/${SOURCE}.o")
done

echo "LD ferro"
"${LD}" "${LDFLAGS[@]}" "${OBJECTS[@]}" -o "${BUILD_DIR}/ferro" || die
