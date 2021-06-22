#!/bin/bash

SCRIPT_PATH="$(cd "$(dirname "$0")" >/dev/null 2>&1; pwd -P)"
SOURCE_ROOT="${SCRIPT_PATH}/../.."
KERNEL_SOURCE_ROOT="${SOURCE_ROOT}/kernel"

source "${SOURCE_ROOT}/scripts/util.sh"
source "${SOURCE_ROOT}/scripts/compile-commands.sh"

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
	-g
	-target ${ARCH}-unknown-none-elf
)
LDFLAGS=(
)

SOURCES=(
	src/core/console.c
	src/core/framebuffer.c
	src/libk/libk.c
)

SOURCES_x86_64=(
	src/core/x86_64/entry.c
)

SOURCES_aarch64=(
	src/core/aarch64/entry.c
)

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
	LDFLAGS_ALL+=(
		"-T${KERNEL_SOURCE_ROOT}/scripts/${ARCH}/kernel.lds"
	)
}

compile() {
	echo "$(color-blue CC) ${1}.o"
	mkdir -p "${CURRENT_BUILD_DIR}/$(dirname "${1}")" || command-failed
	run-command "${CC}" "${CFLAGS_ALL[@]}" -c "${KERNEL_SOURCE_ROOT}/${1}" -o "${CURRENT_BUILD_DIR}/${1}.o" || command-failed
	add-compile-command "${KERNEL_SOURCE_ROOT}/${1}" "$(echo "${CC}" "${CFLAGS_ALL[@]}" -c "${KERNEL_SOURCE_ROOT}/${1}" -o "${CURRENT_BUILD_DIR}/${1}.o")"
}

OBJECTS=()

if [[ "${LD}" =~ ^lld.* ]]; then
	LDFLAGS=(
		-flavor ld
		${LDFLAGS[@]}
	)

	# actually, llvm's ld messes up the output somehow, so disallow it
	die-red "Anillo OS cannot be compiled with LLVM ld"
fi

#
# this is where the compilation actually starts
#

mkdir -p "${CURRENT_BUILD_DIR}"

pushd "${CURRENT_BUILD_DIR}" >/dev/null

"${SCRIPT_PATH}/process-font.py"

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

ORIG_ARCH="${ARCH}"

# add in compile commands for all the other architectures
# (this does not build them)
for ARCH in "${SUPPORTED_ARCHS[@]}"; do
	if [ "${ARCH}" == "${ORIG_ARCH}" ]; then
		continue
	fi

	generate-cflags-all

	ARCH_SOURCES_ALL="SOURCES_${ARCH}[@]"
	for SOURCE in "${!ARCH_SOURCES_ALL}"; do
		add-compile-command "${KERNEL_SOURCE_ROOT}/${SOURCE}" "$(echo "${CC}" "${CFLAGS_ALL[@]}" -c "${KERNEL_SOURCE_ROOT}/${SOURCE}" -o "${CURRENT_BUILD_DIR}/${SOURCE}.o")"
	done
done

ARCH="${ORIG_ARCH}"

# generate the full list of ldflags for the current architecture
generate-ldflags-all

echo "$(color-blue LD) ferro"
run-command "${LD}" "${LDFLAGS_ALL[@]}" "${OBJECTS[@]}" -o "${CURRENT_BUILD_DIR}/ferro" || die

popd >/dev/null
