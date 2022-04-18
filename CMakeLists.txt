cmake_minimum_required(VERSION 3.20)

add_compile_options(
	-ferror-limit=0
)

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

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
	add_compile_options(
		-fno-omit-frame-pointer
	)
endif()

include(${CMAKE_CURRENT_LIST_DIR}/cmake/add_to_ramdisk.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/cmake/add_anillo_dylib.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/cmake/add_anillo_executable.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/cmake/dsym.cmake)

find_dsymutil()

set(COMPILER_RT_FLAGS "-ffreestanding -U__APPLE__ -D__ANILLO__=1 -mmacosx-version-min=10.15")
set(COMPILER_RT_LDFLAGS "--ld-path=${LD_PATH} -Wl,-platform_version,macos,10.15,10.15 -nostdlib -ffreestanding -mmacosx-version-min=10.15")

find_program(LLVM_CONFIG_PATH llvm-config REQUIRED)

include(ExternalProject)

ExternalProject_Add(compiler-rt
	SOURCE_DIR "${CMAKE_SOURCE_DIR}/deps/anillo-compiler-rt"
	BINARY_DIR "${CMAKE_BINARY_DIR}/anillo-compiler-rt"
	CMAKE_ARGS
		"-DCMAKE_AR=${CMAKE_AR}"
		"-DCMAKE_ASM_COMPILER_TARGET=${ANILLO_TARGET_TRIPLE}"
		"-DCMAKE_ASM_FLAGS=${COMPILER_RT_FLAGS}"
		"-DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}"
		"-DCMAKE_C_COMPILER_TARGET=${ANILLO_TARGET_TRIPLE}"
		"-DCMAKE_C_FLAGS=${COMPILER_RT_FLAGS}"
		"-DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
		"-DCMAKE_CXX_COMPILER_TARGET=${ANILLO_TARGET_TRIPLE}"
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
		-DCMAKE_CXX_COMPILER_FORCED=TRUE
		-DCMAKE_C_COMPILER_FORCED=TRUE
	INSTALL_COMMAND ""
	BUILD_BYPRODUCTS "${CMAKE_CURRENT_BINARY_DIR}/anillo-compiler-rt/lib/${ANILLO_TARGET_TRIPLE}/libclang_rt.builtins.a"
)

add_library(compiler-rt-builtins STATIC IMPORTED GLOBAL)
set_target_properties(compiler-rt-builtins PROPERTIES
	IMPORTED_LOCATION "${CMAKE_CURRENT_BINARY_DIR}/anillo-compiler-rt/lib/${ANILLO_TARGET_TRIPLE}/libclang_rt.builtins.a"
)
add_dependencies(compiler-rt-builtins compiler-rt)

include_directories(
	"${CMAKE_CURRENT_BINARY_DIR}/include"
)

add_custom_command(
	OUTPUT
		"${CMAKE_CURRENT_BINARY_DIR}/include/gen/ferro/userspace/syscall-index.h"
		"${CMAKE_CURRENT_BINARY_DIR}/include/gen/ferro/userspace/syscall-handlers.h"
		"${CMAKE_CURRENT_BINARY_DIR}/include/gen/ferro/userspace/syscall-table.h"
		"${CMAKE_CURRENT_BINARY_DIR}/include/gen/libsyscall/syscall-wrappers.h"
		"${CMAKE_CURRENT_BINARY_DIR}/src/gen/libsyscall/syscall-wrappers.c"
	COMMAND
		"${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate-syscalls.py" "${ANILLO_ARCH}" "gen/ferro/userspace/syscall-index.h" "gen/ferro/userspace/syscall-handlers.h" "gen/libsyscall/syscall-wrappers.h" "${CMAKE_CURRENT_BINARY_DIR}/include/gen/ferro/userspace/syscall-index.h" "${CMAKE_CURRENT_BINARY_DIR}/include/gen/ferro/userspace/syscall-handlers.h" "${CMAKE_CURRENT_BINARY_DIR}/include/gen/ferro/userspace/syscall-table.h" "${CMAKE_CURRENT_BINARY_DIR}/include/gen/libsyscall/syscall-wrappers.h" "${CMAKE_CURRENT_BINARY_DIR}/src/gen/libsyscall/syscall-wrappers.c"
	MAIN_DEPENDENCY
		"${CMAKE_CURRENT_SOURCE_DIR}/resources/syscalls/${ANILLO_ARCH}.py"
	DEPENDS
		"${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate-syscalls.py"
		"${CMAKE_CURRENT_SOURCE_DIR}/resources/syscalls/syscall.py"
		"${CMAKE_CURRENT_SOURCE_DIR}/resources/syscalls/common.py"
)

add_custom_target(generate_syscalls
	DEPENDS
		"${CMAKE_CURRENT_BINARY_DIR}/include/gen/ferro/userspace/syscall-index.h"
		"${CMAKE_CURRENT_BINARY_DIR}/include/gen/ferro/userspace/syscall-handlers.h"
		"${CMAKE_CURRENT_BINARY_DIR}/include/gen/ferro/userspace/syscall-table.h"
		"${CMAKE_CURRENT_BINARY_DIR}/include/gen/libsyscall/syscall-wrappers.h"
		"${CMAKE_CURRENT_BINARY_DIR}/src/gen/libsyscall/syscall-wrappers.c"
)

add_custom_command(
	OUTPUT
		"${CMAKE_CURRENT_BINARY_DIR}/include/gen/ferro/offsets.h"
		"${CMAKE_CURRENT_BINARY_DIR}/offsets.json"
		"${CMAKE_CURRENT_BINARY_DIR}/offsets.d"
	COMMAND
		"${CMAKE_CURRENT_SOURCE_DIR}/kernel/scripts/calculate-offsets.py" "${ANILLO_ARCH}" "${CMAKE_CURRENT_BINARY_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/include/gen/ferro/offsets.h" "${CMAKE_CURRENT_BINARY_DIR}/offsets.json" "${CMAKE_CURRENT_BINARY_DIR}/offsets.d"
	MAIN_DEPENDENCY
		"${CMAKE_CURRENT_SOURCE_DIR}/kernel/scripts/calculate-offsets.py"
	DEPFILE
		"${CMAKE_CURRENT_BINARY_DIR}/offsets.d"
)

set(OFFSETS_JSON_PATH "${CMAKE_CURRENT_BINARY_DIR}/offsets.json")

add_custom_target(generate_offsets_header
	DEPENDS
		"${CMAKE_CURRENT_BINARY_DIR}/include/gen/ferro/offsets.h"
)

add_custom_target(generate_ramdisk
	ALL
	COMMAND
		"${CMAKE_CURRENT_SOURCE_DIR}/scripts/build-ramdisk.py" "${CMAKE_CURRENT_BINARY_DIR}/ramdisksrc" "${OFFSETS_JSON_PATH}" "${CMAKE_BINARY_DIR}/ramdisk"
	BYPRODUCTS
		"${CMAKE_BINARY_DIR}/ramdisk"
	DEPENDS
		"${CMAKE_CURRENT_SOURCE_DIR}/scripts/build-ramdisk.py"
		"${OFFSETS_JSON_PATH}"
)

add_subdirectory(kernel)
add_subdirectory(kernel/src/bootstrap/uefi)
add_subdirectory(libsyscall)
add_subdirectory(sysman)
add_subdirectory(dymple)
add_subdirectory(libc)
add_subdirectory(libsys)
add_subdirectory(libsimple)
add_subdirectory(libelf)
add_subdirectory(libmacho)

add_custom_command(
	OUTPUT
		"${CMAKE_CURRENT_BINARY_DIR}/disk.img"
	COMMAND
		"${CMAKE_CURRENT_SOURCE_DIR}/scripts/build-image.py" "${ANILLO_ARCH}" "$<TARGET_FILE:uefi-bootstrap>" "$<TARGET_FILE:ferro>" "${CMAKE_CURRENT_SOURCE_DIR}/imagesrc/startup.nsh" "${CMAKE_CURRENT_SOURCE_DIR}/imagesrc/config.txt" "${CMAKE_CURRENT_BINARY_DIR}/ramdisk" "${CMAKE_CURRENT_BINARY_DIR}/disk.img"
	DEPENDS
		"${CMAKE_CURRENT_SOURCE_DIR}/scripts/build-image.py"
		"$<TARGET_FILE:uefi-bootstrap>"
		"$<TARGET_FILE:ferro>"
		"${CMAKE_CURRENT_SOURCE_DIR}/imagesrc/startup.nsh"
		"${CMAKE_CURRENT_SOURCE_DIR}/imagesrc/config.txt"
		"${CMAKE_BINARY_DIR}/ramdisk"
)

add_custom_target(generate_image
	ALL
	DEPENDS
		"${CMAKE_CURRENT_BINARY_DIR}/disk.img"
)
