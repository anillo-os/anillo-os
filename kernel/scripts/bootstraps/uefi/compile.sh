#!/bin/bash

SCRIPT_PATH="$(cd "$(dirname "$0")" >/dev/null 2>&1; pwd -P)"
SOURCE_ROOT="${SCRIPT_PATH}/../../../.."
KERNEL_SOURCE_ROOT="${SOURCE_ROOT}/kernel"

source "${SOURCE_ROOT}/scripts/util.sh"
source "${SOURCE_ROOT}/scripts/compile-commands.sh"

source "${SOURCE_ROOT}/scripts/find-programs.sh"

CURRENT_BUILD_DIR="${BUILD_DIR}/kernel/src/bootstrap/uefi"
GNU_EFI_INSTALL_ROOT="${BUILD_DIR}/gnu-efi"

#
# flags
#
CFLAGS=(
	"-I${GNU_EFI_INSTALL_ROOT}/usr/local/include/efi"
	"-I${GNU_EFI_INSTALL_ROOT}/usr/local/include/efi/${ARCH}"
	"-I${KERNEL_SOURCE_ROOT}/include"
	-fpic
	-ffreestanding
	-fno-stack-protector
	-fno-stack-check
	-fshort-wchar
	-mno-red-zone
	-c
	-target ${ARCH}
)
LDFLAGS=(
	-shared
	-Bsymbolic
	"-L${GNU_EFI_INSTALL_ROOT}/usr/local/lib"
	"-T${GNU_EFI_INSTALL_ROOT}/usr/local/lib/elf_${ARCH}_efi.lds"
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
	--subsystem=10
)
GNU_EFI_CFLAGS=(
	-target "${ARCH}"
	"-I${KERNEL_SOURCE_ROOT}/deps/elfutils/libelf"
	-fpic
	-fshort-wchar
)

#
# fix up flags
#

# lld (LLVM ld) requires a flavor argument
if [[ "${LD}" =~ ^lld.* ]]; then
	LDFLAGS=(
		-flavor ld
		${LDFLAGS[@]}
	)

	# actually, llvm's ld messes up the output somehow,
	# so disallow it
	die "Anillo OS cannot be compiled with LLVM ld"
fi

# clang is already a cross compiler
if [[ "${CC}" =~ ^clang.* ]]; then
	CFLAGS+=(
		-target ${ARCH}
	)
fi

# x86_64 requires a different target argument
if [ "${ARCH}" == "x86_64" ]; then
	OBJCOPYFLAGS+=(
		--target efi-app-${ARCH}
	)
else
	OBJCOPYFLAGS+=(
		-O binary
	)
fi

OBJECTS=()
STATIC_LIBS=()

compile() {
	echo "$(color-blue CC) ${1}.o"
	mkdir -p "${CURRENT_BUILD_DIR}/$(dirname "${1}")" || command-failed
	run-command "${CC}" "${CFLAGS[@]}" -c "${KERNEL_SOURCE_ROOT}/${1}" -o "${CURRENT_BUILD_DIR}/${1}.o" || command-failed
	add-compile-command "${KERNEL_SOURCE_ROOT}/${1}" "$(echo "${CC}" "${CFLAGS[@]}" -c "${KERNEL_SOURCE_ROOT}/${1}" -o "${CURRENT_BUILD_DIR}/${1}.o")"
}

#
# this is where the compilation actually starts
#

echo "Building gnu-efi..."

pushd "${KERNEL_SOURCE_ROOT}/deps/gnu-efi" >/dev/null
run-command make gnuefi lib "CFLAGS=$(echo ${GNU_EFI_CFLAGS[@]})" CC=$CC >/dev/null
if ! [ $? -eq 0 ]; then
	command-failed
fi

make install "CFLAGS=$(echo ${GNU_EFI_CFLAGS[@]})" CC=$CC INSTALLROOT="${GNU_EFI_INSTALL_ROOT}" >/dev/null
if ! [ $? -eq 0 ]; then
	command-failed
fi
popd >/dev/null

echo "Building UEFI bootstrap..."

mkdir -p "${CURRENT_BUILD_DIR}"

pushd "${CURRENT_BUILD_DIR}" >/dev/null

# gnu-efi objects and libraries
OBJECTS+=("${GNU_EFI_INSTALL_ROOT}/usr/local/lib/crt0-efi-${ARCH}.o")
STATIC_LIBS+=(
	"${GNU_EFI_INSTALL_ROOT}/usr/local/lib/libgnuefi.a"
	"${GNU_EFI_INSTALL_ROOT}/usr/local/lib/libefi.a"
)

compile "src/bootstrap/uefi/main.c"
OBJECTS+=("${CURRENT_BUILD_DIR}/src/bootstrap/uefi/main.c.o")

compile "src/bootstrap/uefi/wrappers.c"
OBJECTS+=("${CURRENT_BUILD_DIR}/src/bootstrap/uefi/wrappers.c.o")

echo "$(color-blue LD) ferro-bootstrap.so"
run-command "${LD}" "${LDFLAGS[@]}" "${OBJECTS[@]}" "${STATIC_LIBS[@]}" -o "${CURRENT_BUILD_DIR}/ferro-bootstrap.so" --defsym=EFI_SUBSYSTEM=10 || command-failed

echo "$(color-blue OBJCOPY) ferro-bootstrap.efi"
run-command "${OBJCOPY}" "${OBJCOPYFLAGS[@]}" "${CURRENT_BUILD_DIR}/ferro-bootstrap.so" "${CURRENT_BUILD_DIR}/ferro-bootstrap.efi" || command-failed

popd >/dev/null
