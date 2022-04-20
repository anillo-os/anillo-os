project(ferro-core C ASM)

include_directories(
	"${CMAKE_CURRENT_BINARY_DIR}/include"
)

add_custom_command(
	OUTPUT
		"${CMAKE_CURRENT_BINARY_DIR}/include/gen/ferro/font.h"
	COMMAND
		"${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/process-font.py" "${CMAKE_CURRENT_SOURCE_DIR}/../../resources/ter-u16n.psf" "${CMAKE_CURRENT_BINARY_DIR}/include/gen/ferro/font.h"
	MAIN_DEPENDENCY
		"${CMAKE_CURRENT_SOURCE_DIR}/../../resources/ter-u16n.psf"
	DEPENDS
		"${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/process-font.py"
)

add_custom_target(generate_font_header
	DEPENDS
		"${CMAKE_CURRENT_BINARY_DIR}/include/gen/ferro/font.h"
)

add_library(ferro_core STATIC
	entry.c
	console.c
	framebuffer.c
	paging.c
	panic.c
	mempool.c
	acpi.c
	timers.c
	scheduler.c
	threads.c
	waitq.c
	workers.c
	config.c
	vfs.c
	ramdisk.c
	per-cpu.c
	ghmap.c
)

add_dependencies(ferro_core
	generate_font_header
)

add_subdirectory("${ANILLO_ARCH}")

target_link_libraries(ferro_core PUBLIC
	ferro_core_${ANILLO_ARCH}
)

target_link_libraries(ferro_core PRIVATE
	libsimple_static_kernel
	libsimple_static_compat
)
