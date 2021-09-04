#!/bin/bash

SCRIPT_PATH="$(cd "$(dirname "$0")" >/dev/null 2>&1; pwd -P)"
SOURCE_ROOT="${SCRIPT_PATH}/.."

source "${SOURCE_ROOT}/scripts/util.sh"
source "${SOURCE_ROOT}/scripts/find-programs.sh"

CURRENT_BUILD_DIR="${BUILD_DIR}/compiler-rt"

COMPILER_RT_SOURCE_DIR="${SOURCE_ROOT}/deps/anillo-compiler-rt"

FLAGS="-ffreestanding -D__ELF__=1"
LDFLAGS="-fuse-ld=lld -nostdlib -ffreestanding"

if [ "${ARCH}" == "aarch64" ]; then
	LDFLAGS="${LDFLAGS} -Wl,-m,aarch64linux"
elif [ "${ARCH}" == "x86_64" ]; then
	LDFLAGS="${LDFLAGS} -Wl,-m,elf_x86_64"
else
	die-red "Unsupported/unrecognized architecture: ${ARCH}"
fi

if ! [ -f "${CURRENT_BUILD_DIR}/CMakeCache.txt" ]; then
	cmake \
		-B "${CURRENT_BUILD_DIR}" \
		-S "${COMPILER_RT_SOURCE_DIR}" \
		"-DCMAKE_AR=${AR}" \
		"-DCMAKE_ASM_COMPILER_TARGET=${ARCH}-unknown-none-elf" \
		"-DCMAKE_ASM_FLAGS=${FLAGS}" \
		"-DCMAKE_C_COMPILER=${CC}" \
		"-DCMAKE_C_COMPILER_TARGET=${ARCH}-unknown-none-elf" \
		"-DCMAKE_C_FLAGS=${FLAGS}" \
		"-DCMAKE_CXX_COMPILER=${CC}" \
		"-DCMAKE_CXX_COMPILER_TARGET=${ARCH}-unknown-none-elf" \
		"-DCMAKE_CXX_FLAGS=${FLAGS}" \
		"-DCMAKE_EXE_LINKER_FLAGS=${LDFLAGS}" \
		"-DCMAKE_NM=${NM}" \
		"-DCMAKE_RANLIB=${RANLIB}" \
		-DCOMPILER_RT_BUILD_BUILTINS=ON \
		-DCOMPILER_RT_BUILD_LIBFUZZER=OFF \
		-DCOMPILER_RT_BUILD_MEMPROF=OFF \
		-DCOMPILER_RT_BUILD_PROFILE=OFF \
		-DCOMPILER_RT_BUILD_SANITIZERS=OFF \
		-DCOMPILER_RT_BUILD_XRAY=OFF \
		-DCOMPILER_RT_BUILD_CRT=OFF \
		-DCOMPILER_RT_DEFAULT_TARGET_ONLY=ON \
		"-DLLVM_CONFIG_PATH=${LLVM_CONFIG}" \
		-DCOMPILER_RT_BAREMETAL_BUILD=1 \
		-DCMAKE_SIZEOF_VOID_P=8 \
		-DCMAKE_SYSTEM_NAME="Anillo" \
		-DLLVM_ENABLE_PER_TARGET_RUNTIME_DIR=1 || command-failed
fi

cmake --build "${CURRENT_BUILD_DIR}" --target builtins || command-failed
