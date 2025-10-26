project(libsys C ASM)

option(LIBSYS_MEMCHECK "Replace libsys's mempool allocator with a memory access checking variant" OFF)

set(libsys_core_sources
	src/core/abort.c
	src/core/console.c
	src/core/format.c
	src/core/general.c
	src/core/ghmap.c
	src/core/handoff.c
	src/core/locks.c
	src/core/mempool.c
	src/core/objects.c
	src/core/pages.c
	src/core/paths.c
	src/core/threads.c
	src/core/config.c
	src/core/once.c
	src/core/channels.c
	src/core/monitors.c
	src/core/counters.c
	src/core/data.c

	src/core/${ANILLO_ARCH}/threads.S
	src/core/${ANILLO_ARCH}/threads.c
	src/core/${ANILLO_ARCH}/ucs.c
	src/core/${ANILLO_ARCH}/ucs.S
)

set(libsys_support_sources
	src/support/vfs.c
	src/support/format.c
	src/support/general.c
	src/support/processes.c
	src/support/sysman.c
)

set(libsys_libs
	libsyscall
)

set(libsys_includes
	include
)

add_anillo_dylib(libsys_core
	RAMDISK
	INSTALL_PATH /sys/lib/libsys/libsys_core.dylib
	SOURCES
		${libsys_core_sources}
)

add_anillo_dylib(libsys_support
	RAMDISK
	INSTALL_PATH /sys/lib/libsys/libsys_support.dylib
	SOURCES
		${libsys_support_sources}
)

add_anillo_dylib(libsys
	RAMDISK
	INSTALL_PATH /sys/lib/libsys.dylib
)

add_library(libsys_core_static STATIC ${libsys_core_sources})
add_library(libsys_support_static STATIC ${libsys_support_sources})
add_library(libsys_core_dymple STATIC ${libsys_core_sources})
add_library(libsys_support_dymple STATIC ${libsys_support_sources})

if (LIBSYS_MEMCHECK)
	target_compile_definitions(libsys_core PRIVATE
		LIBSYS_MEMCHECK=1
	)
	target_compile_definitions(libsys_core_static PRIVATE
		LIBSYS_MEMCHECK=1
	)
endif()

target_compile_definitions(libsys_core_dymple PRIVATE
	BUILDING_DYMPLE=1
)
target_compile_definitions(libsys_support_dymple PRIVATE
	BUILDING_DYMPLE=1
)

target_compile_definitions(libsys_core_static PRIVATE
	BUILDING_STATIC=1
)

target_compile_definitions(libsys_support_static PRIVATE
	BUILDING_STATIC=1
)

set_target_properties(libsys_core_static PROPERTIES
	OUTPUT_NAME sys_core
)
set_target_properties(libsys_support_static PROPERTIES
	OUTPUT_NAME sys_support
)

set_target_properties(libsys_core_dymple PROPERTIES
	OUTPUT_NAME sys_core_dymple
)
set_target_properties(libsys_support_dymple PROPERTIES
	OUTPUT_NAME sys_support_dymple
)

target_link_libraries(libsys_core PUBLIC
	${libsys_libs}
	libsimple_headers
	ferro_headers_uapi
)
target_link_libraries(libsys_support PUBLIC libsys_core)
target_link_libraries(libsys_core_static PUBLIC
	${libsys_libs}
	libsimple_static
	libsimple_static_compat
	ferro_headers_uapi
)
target_link_libraries(libsys_support_static PUBLIC
	libsys_core_static
	libvfs_static
	libeve_static
	libspooky_static
	libmacho
	liblogman_static
)
target_link_libraries(libsys_core_dymple PUBLIC
	${libsys_libs}
	libsimple_static
	libsimple_static_compat
	ferro_headers_uapi
)
target_link_libraries(libsys_support_dymple PUBLIC
	libsys_core_dymple
	libvfs_static
	libeve_dymple
	libspooky_dymple
	libmacho
)

if (ANILLO_HOST_TESTING)
	target_link_libraries(libsys_core PUBLIC
		libdymple
	)
else()
	target_link_libraries(libsys_core PRIVATE
		libdymple
	)
endif()

target_link_libraries(libsys_core PRIVATE
	libsimple
)
target_link_libraries(libsys_support PRIVATE
	libdymple
	libsimple
	libvfs_static
	libspooky
	libeve
	libmacho
	liblogman_static
)

if (NOT ANILLO_HOST_TESTING)
	target_link_options(libsys_core PRIVATE
		"-Wl,-exported_symbols_list,${CMAKE_CURRENT_SOURCE_DIR}/resources/core-exported-symbols.txt"
	)
	target_link_options(libsys_support PRIVATE
		"-Wl,-exported_symbols_list,${CMAKE_CURRENT_SOURCE_DIR}/resources/support-exported-symbols.txt"
	)
endif()

set(CMAKE_C_LINK_LIBRARY_USING_reexport_link_SUPPORTED TRUE)
if (ANILLO_HOST_TESTING)
	set(CMAKE_C_LINK_LIBRARY_USING_reexport_link
		"LINKER:--no-as-needed"
		"<LINK_ITEM>"
		"LINKER:--as-needed"
	)
else()
	set(CMAKE_C_LINK_LIBRARY_USING_reexport_link "PATH{LINKER:-reexport_library,<LIBRARY>}NAME{LINKER:-reexport-l<LIB_ITEM>}")
endif()

if (ANILLO_HOST_TESTING)
	target_link_libraries(libsys PUBLIC
		libsys_core
		libsys_support
	)
else()
	# NOTE: we use `$<TARGET_FILE:>` for libsys_support since that library publicly links to libsys_core,
	#       which we're reexporting here (and CMake doesn't allow use to link to libsys_core normally AND with `$<LINK_LIBRARY:reexport_link>`)
	target_link_libraries(libsys PRIVATE
		"$<LINK_LIBRARY:reexport_link,libsys_core,$<TARGET_FILE:libsys_support>>"
	)
	add_dependencies(libsys libsys_support)
endif()

target_compile_options(libsys PRIVATE
	-fno-omit-frame-pointer
)
target_compile_options(libsys_core PRIVATE
	-fno-omit-frame-pointer
)
target_compile_options(libsys_support PRIVATE
	-fno-omit-frame-pointer
)

target_compile_options(libsys_core_static PRIVATE
	-fno-omit-frame-pointer
)
target_compile_options(libsys_support_static PRIVATE
	-fno-omit-frame-pointer
)
target_compile_options(libsys_core_dymple PRIVATE
	-fno-omit-frame-pointer
)
target_compile_options(libsys_support_dymple PRIVATE
	-fno-omit-frame-pointer
)

target_include_directories(libsys PUBLIC ${libsys_includes})
target_include_directories(libsys_core PUBLIC ${libsys_includes})
target_include_directories(libsys_support PUBLIC ${libsys_includes})
target_include_directories(libsys_core_static PUBLIC ${libsys_includes})
target_include_directories(libsys_support_static PUBLIC ${libsys_includes})
target_include_directories(libsys_core_dymple PUBLIC ${libsys_includes})
target_include_directories(libsys_support_dymple PUBLIC ${libsys_includes})

target_include_directories(libsys PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/include)
target_include_directories(libsys_core PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/include)
target_include_directories(libsys_support PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/include)
target_include_directories(libsys_core_static PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/include)
target_include_directories(libsys_support_static PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/include)
target_include_directories(libsys_core_dymple PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/include)
target_include_directories(libsys_support_dymple PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/include)

add_library(libsys_headers INTERFACE)
target_include_directories(libsys_headers INTERFACE ${libsys_includes})
target_link_libraries(libsys_headers INTERFACE
	libsimple_headers
	ferro_headers_uapi
)

get_target_property(LIBSYS_INCLUDE_DIRS libsys INCLUDE_DIRECTORIES)

calculate_offsets_base(libsys_offsets resources/offsets.c include/libsys/gen/offsets.h libsys)

add_dependencies(libsys libsys_offsets)
add_dependencies(libsys_core libsys_offsets)
add_dependencies(libsys_support libsys_offsets)
add_dependencies(libsys_core_static libsys_offsets)
add_dependencies(libsys_support_static libsys_offsets)
add_dependencies(libsys_core_dymple libsys_offsets)
add_dependencies(libsys_support_dymple libsys_offsets)

if (NOT ANILLO_HOST_TESTING)
	target_link_options(libsys INTERFACE
		"-Wl,-dylib_file,/sys/lib/libsys/libsys_core.dylib:$<TARGET_FILE:libsys_core>"
		"-Wl,-dylib_file,/sys/lib/libsys/libsys_support.dylib:$<TARGET_FILE:libsys_support>"
	)
endif()

if (ANILLO_HOST_TESTING)
	add_library(libsys_host STATIC
		src/core/objects.c
		src/core/ghmap.c
		src/core/data.c
		src/host/mempool.c
		src/host/console.c
		src/host/abort.c
		src/host/vfs.c
		src/host/pages.c
	)

	target_link_libraries(libsys_host PUBLIC libsys_headers)
endif()

target_link_libraries(libsys_core PUBLIC libvfs_headers)
target_link_libraries(libsys PUBLIC libvfs_headers)
target_link_libraries(libsys_core_static PUBLIC libvfs_headers)
target_link_libraries(libsys_support_static PUBLIC libvfs_headers)
target_link_libraries(libsys_core_dymple PUBLIC libvfs_headers)
target_link_libraries(libsys_headers INTERFACE libvfs_headers)

sanitize_targets(
	libsys
	libsys_core_static
	libsys_support_static
	libsys_core
	libsys_support
)
