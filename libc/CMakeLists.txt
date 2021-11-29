project(libc C)

add_anillo_dylib(libc
	RAMDISK
	INSTALL_PATH /sys/lib/libc.dylib
	SOURCES
		src/stdio.c
		src/stdlib.c
		src/string.c
)

target_link_libraries(libc PRIVATE
	libsys
	libsimple
)

target_link_libraries(libc PUBLIC
	ferro_headers_base
)

target_include_directories(libc PUBLIC
	include
)

target_link_options(libc PRIVATE
	"-Wl,-reexported_symbols_list,${CMAKE_CURRENT_SOURCE_DIR}/reexported-symbols.txt"
	"-Wl,-alias_list,${CMAKE_CURRENT_SOURCE_DIR}/aliased-symbols.txt"
)
