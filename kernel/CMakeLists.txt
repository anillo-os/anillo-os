project(ferro C ASM)

add_link_options(
	-static
	"-T${CMAKE_CURRENT_SOURCE_DIR}/scripts/kernel.lds"
)

add_compile_options(
	-fno-plt
	-fno-pic
	-mcmodel=large
)

include_directories(
	include

	"${CMAKE_CURRENT_BINARY_DIR}/include"
)

add_custom_command(
	OUTPUT
		"${CMAKE_CURRENT_BINARY_DIR}/include/gen/ferro/gdbstub/target.xml.h"
	COMMAND
		"${CMAKE_CURRENT_SOURCE_DIR}/scripts/encode-file.py" "${CMAKE_CURRENT_SOURCE_DIR}/src/gdbstub/${ANILLO_ARCH}/target.xml" "${CMAKE_CURRENT_BINARY_DIR}/include/gen/ferro/gdbstub/target.xml.h" "target_xml_data"
	MAIN_DEPENDENCY
		"${CMAKE_CURRENT_SOURCE_DIR}/src/gdbstub/${ANILLO_ARCH}/target.xml"
	DEPENDS
		"${CMAKE_CURRENT_SOURCE_DIR}/scripts/encode-file.py"
)

add_custom_target(generate_target_xml_header
	DEPENDS
		"${CMAKE_CURRENT_BINARY_DIR}/include/gen/ferro/gdbstub/target.xml.h"
)

cmake_policy(SET CMP0116 NEW)

add_custom_command(
	OUTPUT
		"${CMAKE_CURRENT_BINARY_DIR}/include/gen/ferro/offsets.h"
		"${CMAKE_CURRENT_BINARY_DIR}/offsets.json"
		"${CMAKE_CURRENT_BINARY_DIR}/offsets.d"
	COMMAND
		"${CMAKE_CURRENT_SOURCE_DIR}/scripts/calculate-offsets.py" "${ANILLO_ARCH}" "${CMAKE_CURRENT_BINARY_DIR}/include/gen/ferro/offsets.h" "${CMAKE_CURRENT_BINARY_DIR}/offsets.json" "${CMAKE_CURRENT_BINARY_DIR}/offsets.d"
	MAIN_DEPENDENCY
		"${CMAKE_CURRENT_SOURCE_DIR}/scripts/calculate-offsets.py"
	DEPFILE
		"${CMAKE_CURRENT_BINARY_DIR}/offsets.d"
)

set(OFFSETS_JSON_PATH "${CMAKE_CURRENT_BINARY_DIR}/offsets.json" PARENT_SCOPE)

add_custom_target(generate_offsets_header
	DEPENDS
		"${CMAKE_CURRENT_BINARY_DIR}/include/gen/ferro/offsets.h"
)

add_custom_command(
	OUTPUT
		"${CMAKE_CURRENT_BINARY_DIR}/include/gen/ferro/font.h"
	COMMAND
		"${CMAKE_CURRENT_SOURCE_DIR}/scripts/process-font.py" "${CMAKE_CURRENT_SOURCE_DIR}/resources/Lat15-TerminusBold32x16.psf" "${CMAKE_CURRENT_BINARY_DIR}/include/gen/ferro/font.h"
	MAIN_DEPENDENCY
		"${CMAKE_CURRENT_SOURCE_DIR}/resources/Lat15-TerminusBold32x16.psf"
	DEPENDS
		"${CMAKE_CURRENT_SOURCE_DIR}/scripts/process-font.py"
)

add_custom_target(generate_font_header
	DEPENDS
		"${CMAKE_CURRENT_BINARY_DIR}/include/gen/ferro/font.h"
)

add_executable(ferro
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
	src/core/vfs.c
	src/core/ramdisk.c

	src/libk/libk.c

	src/gdbstub/main.c
	src/gdbstub/packet-buffer.c
	src/gdbstub/malloc.c
)

add_dependencies(ferro
	generate_target_xml_header
	generate_offsets_header
	generate_font_header
)

if ("${ANILLO_ARCH}" STREQUAL "x86_64")
	target_sources(ferro PRIVATE
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

		src/gdbstub/x86_64/registers.c
	)
elseif ("${ANILLO_ARCH}" STREQUAL "aarch64")
	target_sources(ferro PRIVATE
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

		src/gdbstub/aarch64/registers.c
	)
endif()

target_link_libraries(ferro PRIVATE
	compiler-rt-builtins
)
