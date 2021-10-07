cmake_minimum_required(VERSION 3.20)

if (NOT DEFINED ANILLO_ARCH)
	execute_process(
		COMMAND uname -m
		OUTPUT_VARIABLE ANILLO_ARCH
		ERROR_VARIABLE ANILLO_ARCH
	)

	if (DEFINED ARCH)
		set(ANILLO_ARCH "${ARCH}")
	elseif (DEFINED ENV{ANILLO_ARCH})
		set(ANILLO_ARCH "$ENV{ANILLO_ARCH}")
	elseif (DEFINED ENV{ARCH})
		set(ANILLO_ARCH "$ENV{ARCH}")
	endif()
endif()

string(STRIP "${ANILLO_ARCH}" ANILLO_ARCH)

set(ANILLO_SUPPORTED_ARCHS
	x86_64
	aarch64
)

if (NOT ("${ANILLO_ARCH}" IN_LIST ANILLO_SUPPORTED_ARCHS))
	message(FATAL_ERROR "Unsupported arch: ${ANILLO_ARCH}")
endif()

set(CMAKE_TOOLCHAIN_FILE "cmake/toolchains/${ANILLO_ARCH}.cmake")

if (NOT DEFINED CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE Release)
endif()

project(anillo-os)

set(COMPILER_RT_FLAGS "-ffreestanding -D__ELF__=1")
set(COMPILER_RT_LDFLAGS "-fuse-ld=lld -nostdlib -ffreestanding")

if ("${ANILLO_ARCH}" STREQUAL "aarch64")
	set(COMPILER_RT_LDFLAGS "${COMPILER_RT_LDFLAGS} -Wl,-m,aarch64linux")
elseif ("${ANILLO_ARCH}" STREQUAL "x86_64")
	set(COMPILER_RT_LDFLAGS "${COMPILER_RT_LDFLAGS} -Wl,-m,elf_x86_64")
endif()

find_program(LLVM_CONFIG_PATH llvm-config)

include(ExternalProject)

ExternalProject_Add(compiler-rt
	SOURCE_DIR "${CMAKE_SOURCE_DIR}/deps/anillo-compiler-rt"
	BINARY_DIR "${CMAKE_BINARY_DIR}/anillo-compiler-rt"
	CMAKE_ARGS
		"-DCMAKE_AR=${CMAKE_AR}"
		"-DCMAKE_ASM_COMPILER_TARGET=${ANILLO_ARCH}-unknown-none-elf"
		"-DCMAKE_ASM_FLAGS=${COMPILER_RT_FLAGS}"
		"-DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}"
		"-DCMAKE_C_COMPILER_TARGET=${ANILLO_ARCH}-unknown-none-elf"
		"-DCMAKE_C_FLAGS=${COMPILER_RT_FLAGS}"
		"-DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
		"-DCMAKE_CXX_COMPILER_TARGET=${ANILLO_ARCH}-unknown-none-elf"
		"-DCMAKE_CXX_FLAGS=${COMPILER_RT_FLAGS}"
		"-DCMAKE_EXE_LINKER_FLAGS=${COMPILER_RT_LDFLAGS}"
		"-DCMAKE_NM=${CMAKE_NM}"
		"-DCMAKE_RANLIB=${CMAKE_RANLIB}"
		-DCOMPILER_RT_BUILD_BUILTINS=ON
		-DCOMPILER_RT_BUILD_LIBFUZZER=OFF
		-DCOMPILER_RT_BUILD_MEMPROF=OFF
		-DCOMPILER_RT_BUILD_PROFILE=OFF
		-DCOMPILER_RT_BUILD_SANITIZERS=OFF
		-DCOMPILER_RT_BUILD_XRAY=OFF
		-DCOMPILER_RT_BUILD_CRT=OFF
		-DCOMPILER_RT_DEFAULT_TARGET_ONLY=ON
		"-DLLVM_CONFIG_PATH=${LLVM_CONFIG_PATH}"
		-DCOMPILER_RT_BAREMETAL_BUILD=1
		"-DCMAKE_SIZEOF_VOID_P=${CMAKE_SIZEOF_VOID_P}"
		"-DCMAKE_SYSTEM_NAME=${CMAKE_SYSTEM_NAME}"
		-DLLVM_ENABLE_PER_TARGET_RUNTIME_DIR=1
	INSTALL_COMMAND ""
	BUILD_BYPRODUCTS "${CMAKE_CURRENT_BINARY_DIR}/anillo-compiler-rt/lib/${ANILLO_ARCH}-unknown-none-elf/libclang_rt.builtins.a"
)

add_library(compiler-rt-builtins STATIC IMPORTED GLOBAL)
set_target_properties(compiler-rt-builtins PROPERTIES
	IMPORTED_LOCATION "${CMAKE_CURRENT_BINARY_DIR}/anillo-compiler-rt/lib/${ANILLO_ARCH}-unknown-none-elf/libclang_rt.builtins.a"
)

add_subdirectory(kernel)
add_subdirectory(kernel/src/bootstrap/uefi)

add_custom_command(
	OUTPUT
		"${CMAKE_CURRENT_BINARY_DIR}/ramdisk"
	COMMAND
		"${CMAKE_CURRENT_SOURCE_DIR}/scripts/build-ramdisk.py" "${CMAKE_CURRENT_BINARY_DIR}/ramdisksrc" "${OFFSETS_JSON_PATH}" "${CMAKE_CURRENT_BINARY_DIR}/ramdisk"
	DEPENDS
		"${CMAKE_CURRENT_SOURCE_DIR}/scripts/build-ramdisk.py"
		"${OFFSETS_JSON_PATH}"
)

add_custom_target(generate_ramdisk
	ALL
	DEPENDS
		"${CMAKE_CURRENT_BINARY_DIR}/ramdisk"
)

add_custom_command(
	OUTPUT
		"${CMAKE_CURRENT_BINARY_DIR}/disk.img"
	COMMAND
		"${CMAKE_CURRENT_SOURCE_DIR}/scripts/build-image.py" "$<TARGET_FILE:uefi-bootstrap>" "$<TARGET_FILE:ferro>" "${CMAKE_CURRENT_SOURCE_DIR}/imagesrc/startup.nsh" "${CMAKE_CURRENT_SOURCE_DIR}/imagesrc/config.txt" "${CMAKE_CURRENT_BINARY_DIR}/ramdisk" "${CMAKE_CURRENT_BINARY_DIR}/disk.img"
	DEPENDS
		"${CMAKE_CURRENT_SOURCE_DIR}/scripts/build-image.py"
		"$<TARGET_FILE:uefi-bootstrap>"
		"$<TARGET_FILE:ferro>"
		"${CMAKE_CURRENT_SOURCE_DIR}/imagesrc/startup.nsh"
		"${CMAKE_CURRENT_SOURCE_DIR}/imagesrc/config.txt"
		"${CMAKE_CURRENT_BINARY_DIR}/ramdisk"
)

add_custom_target(generate_image
	ALL
	DEPENDS
		"${CMAKE_CURRENT_BINARY_DIR}/disk.img"
)
