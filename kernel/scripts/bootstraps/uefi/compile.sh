#!/bin/bash

SCRIPT_PATH="$(cd "$(dirname "$0")" >/dev/null 2>&1; pwd -P)"
KERNEL_SOURCE_ROOT="${SCRIPT_PATH}/../../.."
ARCH=x86_64
CC=clang
LD=ld
OBJCOPY=objcopy
BUILD_DIR="${KERNEL_SOURCE_ROOT}/build/src/bootstrap/uefi"
CFLAGS=(
	"-I${KERNEL_SOURCE_ROOT}/deps/gnu-efi/inc"
	"-I${KERNEL_SOURCE_ROOT}/deps/gnu-efi/inc/${ARCH}"
	"-I${KERNEL_SOURCE_ROOT}/include"
	-fpic
	-ffreestanding
	-fno-stack-protector
	-fno-stack-check
	-fshort-wchar
	-mno-red-zone
	-c
)
LDFLAGS=(
	-shared
	-Bsymbolic
	"-L${KERNEL_SOURCE_ROOT}/deps/gnu-efi/${ARCH}/lib"
	"-L${KERNEL_SOURCE_ROOT}/deps/gnu-efi/${ARCH}/gnuefi"
	"-T${KERNEL_SOURCE_ROOT}/deps/gnu-efi/gnuefi/elf_${ARCH}_efi.lds"
)
OBJCOPYFLAGS=(
	-j .text
	-j .sdata
	-j .data
	-j .dynamic
	-j .dynsym
	-j .rel
	-j .rela
	-j .rel.*
	-j .rela.*
	-j .reloc
	--target efi-app-${ARCH}
	--subsystem=10
)

die() {
	echo "Command failed"
	exit 1
}

echo "Building gnu-efi..."

pushd "${KERNEL_SOURCE_ROOT}/deps/gnu-efi" >/dev/null
make gnuefi lib >/dev/null
popd >/dev/null

echo "Building UEFI bootstrap..."

mkdir -p "${BUILD_DIR}"

pushd "${BUILD_DIR}" >/dev/null

echo "CC main.c.o"
"${CC}" "${CFLAGS[@]}" "${KERNEL_SOURCE_ROOT}/src/bootstrap/uefi/main.c" -o main.c.o || die

echo "CC wrappers.c.o"
"${CC}" "${CFLAGS[@]}" "${KERNEL_SOURCE_ROOT}/src/bootstrap/uefi/wrappers.c" -o wrappers.c.o || die

echo "LD ferro-bootstrap.so"
"${LD}" "${LDFLAGS[@]}" "${KERNEL_SOURCE_ROOT}/deps/gnu-efi/${ARCH}/gnuefi/crt0-efi-${ARCH}.o" main.c.o wrappers.c.o -o ferro-bootstrap.so -lgnuefi -lefi || die

echo "OBJCOPY ferro-bootstrap.efi"
"${OBJCOPY}" "${OBJCOPYFLAGS[@]}" ferro-bootstrap.so ferro-bootstrap.efi || die

popd >/dev/null
