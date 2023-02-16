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

include_directories(
	"${CMAKE_CURRENT_BINARY_DIR}/include"
)

add_subdirectory(bootstrap/uefi)
