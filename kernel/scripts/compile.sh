#!/bin/bash

SCRIPT_PATH="$(cd "$(dirname "$0")" >/dev/null 2>&1; pwd -P)"
SOURCE_ROOT="${SCRIPT_PATH}/../.."
KERNEL_SOURCE_ROOT="${SOURCE_ROOT}/kernel"

source "${SOURCE_ROOT}/scripts/util.sh"

#
# build configuration
#
if [ -z "${USE_UBSAN}" ]; then
	USE_UBSAN=0
fi

if [ -z "${USE_UBSAN_MINIMAL}" ]; then
	USE_UBSAN_MINIMAL=0
fi

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
)
ASFLAGS=(
	"-I${KERNEL_SOURCE_ROOT}/include"
	-ggdb3
	-fno-pic
)
LDFLAGS=(
	"-fuse-ld=lld"
	#-Wl,-flavor,link
	-ggdb3
	-ffreestanding
	-nostdlib
	-static
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
	src/core/scheduler.c
	src/core/threads.c
	src/core/waitq.c
	src/core/workers.c
	src/core/config.c

	src/libk/libk.c
)

SOURCES_x86_64=(
	src/core/x86_64/paging.c
	src/core/x86_64/interrupts.c
	src/core/x86_64/per-cpu.c
	src/core/x86_64/apic.c
	src/core/x86_64/tsc.c
	src/core/x86_64/scheduler.c
	src/core/x86_64/threads.c
	src/core/x86_64/serial.c

	src/core/generic/locks.c

	src/core/x86_64/interrupt-wrappers.S
	src/core/x86_64/scheduler-helpers.S
	src/core/x86_64/thread-runner.S
)

SOURCES_aarch64=(
	src/core/aarch64/paging.c
	src/core/aarch64/interrupts.c
	src/core/aarch64/per-cpu.c
	src/core/aarch64/generic-timer.c
	src/core/aarch64/gic.c
	src/core/aarch64/scheduler.c
	src/core/aarch64/threads.c
	src/core/aarch64/acpi.c
	src/core/aarch64/serial.c

	src/core/generic/locks.c

	src/core/aarch64/ivt.S
	src/core/aarch64/thread-runner.S
	src/core/aarch64/scheduler-helpers.S
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
	LDFLAGS_ALL+=(
		-target "${ARCH}-unknown-none-elf"
	)
}

# generates ASFLAGS_ALL, containing the full list of ASFLAGS for the current architecture
generate-asflags-all() {
	# these are architecture-independent, manually maintained flags
	ASFLAGS_ALL=(
		"${ASFLAGS[@]}"
	)

	# these are architecture-dependent, manually maintained flags
	ASFLAGS_ARCH_ALL="ASFLAGS_${ARCH}[@]"
	ASFLAGS_ALL+=(
		"${!ASFLAGS_ARCH_ALL}"
	)

	# these are architecture-dependent, automatically generated flags
	ASFLAGS_ALL+=(
		"-I${BUILD_ROOT}/${ARCH}/kernel/include"
		-target "${ARCH}-unknown-none-elf"
	)
}

compile() {
	EXTENSION="$(extname "${1}")"

	if [ "x${ANILLO_GENERATING_COMPILE_COMMANDS}" == "x1" ]; then
		case "${EXTENSION}" in
			s|S)
				add-compile-command "${KERNEL_SOURCE_ROOT}/${1}" "$(echo "${CC}" "${ASFLAGS_ALL[@]}" -c "${KERNEL_SOURCE_ROOT}/${1}" -o "${CURRENT_BUILD_DIR}/${1}.o")"
				;;

			c)
				add-compile-command "${KERNEL_SOURCE_ROOT}/${1}" "$(echo "${CC}" "${CFLAGS_ALL[@]}" -c "${KERNEL_SOURCE_ROOT}/${1}" -o "${CURRENT_BUILD_DIR}/${1}.o")"
				;;

			*)
				echo "$(color-yellow "warning: Unsupported/unrecognized file extension \"${EXTENSION}\" (for file \"${1}\")")"
		esac
	else
		mkdir -p "${CURRENT_BUILD_DIR}/$(dirname "${1}")" || command-failed

		case "${EXTENSION}" in
			s|S)
				echo "$(color-blue CC-AS) ${1}.o"
				run-command "${CC}" "${ASFLAGS_ALL[@]}" -c "${KERNEL_SOURCE_ROOT}/${1}" -o "${CURRENT_BUILD_DIR}/${1}.o" || command-failed
				;;

			c)
				echo "$(color-blue CC) ${1}.o"
				run-command "${CC}" "${CFLAGS_ALL[@]}" -c "${KERNEL_SOURCE_ROOT}/${1}" -o "${CURRENT_BUILD_DIR}/${1}.o" || command-failed
				;;

			*)
				die-red "Unsupported/unrecognized file extension \"${EXTENSION}\" (for file \"${1}\")"
				;;
		esac
	fi
}

OBJECTS=(
	# statically link the compiler-rt builtins library
	"${COMPILER_RT_BUILTINS}"
)

#
# configuration dependent stuff
#

if [ "${USE_UBSAN}" -eq 1 ]; then
	SOURCES+=(
		src/ubsan/ubsan.c
	)
	CFLAGS+=(
		-fsanitize=undefined
	)

	if [ "${USE_UBSAN_MINIMAL}" -eq 1 ]; then
		CFLAGS+=(
			-fsanitize-minimal-runtime
			-DUBSAN_MINIMAL=1
		)
	fi
fi

#
# this is where the compilation actually starts
#

mkdir -p "${CURRENT_BUILD_DIR}"

pushd "${CURRENT_BUILD_DIR}" >/dev/null

if ! [ "x${ANILLO_GENERATING_COMPILE_COMMANDS}" == "x1" ]; then
	"${SCRIPT_PATH}/generate.sh"
fi

# generate the full list of cflags and asflags for the current architecture
generate-cflags-all
generate-asflags-all

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
