#!/bin/bash

SCRIPT_PATH="$(cd "$(dirname "$0")" >/dev/null 2>&1; pwd -P)"
SOURCE_ROOT="${SCRIPT_PATH}/../.."
KERNEL_SOURCE_ROOT="${SOURCE_ROOT}/kernel"

source "${SOURCE_ROOT}/scripts/util.sh"

#
# programs
#
source "${SOURCE_ROOT}/scripts/find-programs.sh"

CURRENT_BUILD_DIR="${BUILD_DIR}/kernel"

#
# flags
#
CFLAGS=(
	"-I${KERNEL_SOURCE_ROOT}/include"
	-fno-asynchronous-unwind-tables
	-ffreestanding
	-fno-stack-protector
	-fno-stack-check
	-fno-omit-frame-pointer
	-mno-red-zone
	-mcmodel=large
	-ggdb3
	-fno-plt
	-fno-pic
	-target ${ARCH}-unknown-none-elf
)
LDFLAGS=(
	"-fuse-ld=lld"
	#-Wl,-flavor,link
	-ggdb3
	-ffreestanding
	-nostdlib
	-static
	-target ${ARCH}-unknown-none-elf
	"-T${KERNEL_SOURCE_ROOT}/scripts/kernel.lds"
)
LDFLAGS_x86_64=(
	-Wl,-m,elf_x86_64
)
LDFLAGS_aarch64=(
	-Wl,-m,aarch64linux
)

SOURCES=(
	src/core/entry.c
	src/core/console.c
	src/core/framebuffer.c
	src/core/paging.c
	src/core/panic.c
	src/core/mempool.c
	src/core/acpi.c
	src/core/timers.c
	src/libk/libk.c
)

SOURCES_x86_64=(
	src/core/x86_64/paging.c
	src/core/x86_64/interrupts.c
	src/core/x86_64/locks.c
	src/core/x86_64/per-cpu.c
	src/core/x86_64/apic.c
	src/core/x86_64/tsc.c
)

SOURCES_aarch64=(
	src/core/aarch64/paging.c
	src/core/aarch64/interrupts.c
	src/core/aarch64/locks.c
	src/core/aarch64/per-cpu.c
	src/core/aarch64/ivt.s
	src/core/aarch64/generic-timer.c
	src/core/aarch64/gic.c
)

COMPILER_RT_BUILTINS="${BUILD_DIR}/compiler-rt/lib/${ARCH}-unknown-none-elf/libclang_rt.builtins.a"

# generates CFLAGS_ALL, containing the full list of CFLAGS for the current architecture
generate-cflags-all() {
	# these are architecture-independent, manually maintained flags
	CFLAGS_ALL=(
		"${CFLAGS[@]}"
	)

	# these are architecture-dependent, manually maintained flags
	CFLAGS_ARCH_ALL="CFLAGS_${ARCH}[@]"
	CFLAGS_ALL+=(
		"${!CFLAGS_ARCH_ALL}"
	)

	# these are architecture-dependent, automatically generated flags
	CFLAGS_ALL+=(
		"-I${BUILD_ROOT}/${ARCH}/kernel/include"
		-target "${ARCH}-unknown-none-elf"
	)
}

# generates LDFLAGS_ALL, containing the full list of LDFLAGS for the current architecture
generate-ldflags-all() {
	# these are architecture-independent, manually maintained flags
	LDFLAGS_ALL=(
		"${LDFLAGS[@]}"
	)

	# these are architecture-dependent, manually maintained flags
	LDFLAGS_ARCH_ALL="LDFLAGS_${ARCH}[@]"
	LDFLAGS_ALL+=(
		"${!LDFLAGS_ARCH_ALL}"
	)

	# these are architecture-dependent, automatically generated flags
	LDFLAGS_ALL+=()
}

compile() {
	if [ "x${ANILLO_GENERATING_COMPILE_COMMANDS}" == "x1" ]; then
		add-compile-command "${KERNEL_SOURCE_ROOT}/${1}" "$(echo "${CC}" "${CFLAGS_ALL[@]}" -c "${KERNEL_SOURCE_ROOT}/${1}" -o "${CURRENT_BUILD_DIR}/${1}.o")"
	else
		echo "$(color-blue CC) ${1}.o"
		mkdir -p "${CURRENT_BUILD_DIR}/$(dirname "${1}")" || command-failed
		run-command "${CC}" "${CFLAGS_ALL[@]}" -c "${KERNEL_SOURCE_ROOT}/${1}" -o "${CURRENT_BUILD_DIR}/${1}.o" || command-failed
	fi
}

OBJECTS=(
	# statically link the compiler-rt builtins library
	"${COMPILER_RT_BUILTINS}"
)

#
# this is where the compilation actually starts
#

mkdir -p "${CURRENT_BUILD_DIR}"

pushd "${CURRENT_BUILD_DIR}" >/dev/null

if ! [ "x${ANILLO_GENERATING_COMPILE_COMMANDS}" == "x1" ]; then
	"${SCRIPT_PATH}/generate.sh"
fi

# generate the full list of cflags for the current architecture
generate-cflags-all

for SOURCE in "${SOURCES[@]}"; do
	compile "${SOURCE}" || die
	OBJECTS+=("${CURRENT_BUILD_DIR}/${SOURCE}.o")
done

ARCH_SOURCES_ALL="SOURCES_${ARCH}[@]"
for SOURCE in "${!ARCH_SOURCES_ALL}"; do
	compile "${SOURCE}" || die
	OBJECTS+=("${CURRENT_BUILD_DIR}/${SOURCE}.o")
done

# generate the full list of ldflags for the current architecture
generate-ldflags-all

if ! [ "x${ANILLO_GENERATING_COMPILE_COMMANDS}" == "x1" ]; then
	echo "$(color-blue CC-LD) ferro"
	run-command "${CC}" "${LDFLAGS_ALL[@]}" "${OBJECTS[@]}" -o "${CURRENT_BUILD_DIR}/ferro" || die
fi

popd >/dev/null
