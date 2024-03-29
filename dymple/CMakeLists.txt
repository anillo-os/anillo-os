project(dymple C ASM)

# Dymple produces a LOT of debug output when debug logging is enabled, so disable it by default
# (even in debug builds)
option(DYMPLE_LOG_DEBUG_ENABLED "Whether to enable debug output in Dymple" OFF)

# only "ON" for debugging
option(DYMPLE_PRINT_LOAD_ADDRESSES "Whether to print library load addresses when they are loaded" ON)

include_directories(
	include
	private-include
)

add_executable(dymple
	src/loader/api.c
	src/loader/dymple.c
	src/loader/images.c
	src/loader/log.c
	src/loader/relocations.c
	src/loader/resolution.c
	src/loader/${ANILLO_ARCH}/stub-binder.S
)

target_include_directories(dymple PRIVATE
	include
	private-include
)

target_link_libraries(dymple PUBLIC
	libsys_core_dymple
	libsys_support_dymple
	libsimple_static
	libsimple_static_compat
	libmacho
)

add_to_ramdisk(
	TARGET dymple
	DESTINATION /sys/support/dymple
)

add_anillo_dylib(libdymple
	RAMDISK
	INSTALL_PATH /sys/lib/libdymple.dylib
	SOURCES
		src/stub/${ANILLO_ARCH}/stubs.S
		"${CMAKE_SOURCE_DIR}/cmake/dummy.c"
)

add_library(libstackchk_stub_static STATIC
	src/stackchk_stub/${ANILLO_ARCH}/stubs.S
)

set_target_properties(libstackchk_stub_static PROPERTIES
	OUTPUT_NAME stackchk_stub
)

target_include_directories(libdymple PUBLIC include)
target_link_libraries(libdymple INTERFACE libsys_headers)

target_compile_options(dymple PRIVATE
	-fPIC
	-fno-stack-protector
	-fno-omit-frame-pointer
	-mno-implicit-float
)

target_link_options(dymple PRIVATE
	-Wl,-dylinker
	-Wl,-dylinker_install_name,/sys/support/dymple
	-fvisibility=hidden

	# shift the base over so that NULL is actually an invalid address (and produces a segfault)
	-Wl,-image_base,0x1000
)

if (DYMPLE_LOG_DEBUG_ENABLED)
	target_compile_definitions(dymple PRIVATE DYMPLE_LOG_DEBUG_ENABLED=1)
else()
	target_compile_definitions(dymple PRIVATE DYMPLE_LOG_DEBUG_ENABLED=0)
endif()

if (DYMPLE_PRINT_LOAD_ADDRESSES)
	target_compile_definitions(dymple PRIVATE DYMPLE_PRINT_LOAD_ADDRESSES=1)
else()
	target_compile_definitions(dymple PRIVATE DYMPLE_PRINT_LOAD_ADDRESSES=0)
endif()

dsym(dymple)
