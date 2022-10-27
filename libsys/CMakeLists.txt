project(libsys C ASM)

set(libsys_sources
	src/abort.c
	src/console.c
	src/files.c
	src/format.c
	src/general.c
	src/ghmap.c
	src/handoff.c
	src/locks.c
	src/mempool.c
	src/objects.c
	src/pages.c
	src/paths.c
	src/streams.c
	src/threads.c
	src/config.c
	src/once.c
	src/processes.c
	src/channels.c
	src/monitors.c
	src/counters.c

	src/${ANILLO_ARCH}/threads.S
	src/${ANILLO_ARCH}/threads.c
	src/${ANILLO_ARCH}/ucs.c
	src/${ANILLO_ARCH}/ucs.S
)

set(libsys_libs
	libsyscall
)

set(libsys_includes
	include
)

add_anillo_dylib(libsys
	RAMDISK
	INSTALL_PATH /sys/lib/libsys.dylib
	SOURCES
		${libsys_sources}
)
add_library(libsys_static STATIC ${libsys_sources})

set_target_properties(libsys_static PROPERTIES
	OUTPUT_NAME sys
)

target_link_libraries(libsys PUBLIC
	${libsys_libs}
	libsimple_headers
	ferro_headers_uapi
)
target_link_libraries(libsys_static PUBLIC
	${libsys_libs}
	libsimple_static
	libsimple_static_compat
	ferro_headers_uapi
)

target_link_libraries(libsys PRIVATE
	libdymple
	libsimple
)

target_link_options(libsys PRIVATE
	"-Wl,-exported_symbols_list,${CMAKE_CURRENT_SOURCE_DIR}/exported-symbols.txt"
)

target_compile_options(libsys PRIVATE
	-fno-omit-frame-pointer
)

target_compile_options(libsys_static PRIVATE
	-fno-omit-frame-pointer
)

target_include_directories(libsys PUBLIC ${libsys_includes})
target_include_directories(libsys_static PUBLIC ${libsys_includes})

target_include_directories(libsys PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/include)
target_include_directories(libsys_static PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/include)

add_library(libsys_headers INTERFACE)
target_include_directories(libsys_headers INTERFACE ${libsys_includes})
target_link_libraries(libsys_headers INTERFACE
	libsimple_headers
	ferro_headers_uapi
)

get_target_property(LIBSYS_INCLUDE_DIRS libsys INCLUDE_DIRECTORIES)

calculate_offsets_base(libsys_offsets resources/offsets.c include/libsys/gen/offsets.h libsys)

add_dependencies(libsys libsys_offsets)
add_dependencies(libsys_static libsys_offsets)
