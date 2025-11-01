project(libjson C)

set(libjson_sources
	src/lex.c
	src/parse.c
	src/dump.c
	src/objects.c

	src/null.c
	src/bool.c
	src/string.c
	src/number.c
	src/dict.c
	src/array.c
)

set(libjson_includes
	include
)

add_anillo_dylib(libjson
	RAMDISK
	INSTALL_PATH /sys/lib/libjson.dylib
	SOURCES
		${libjson_sources}
)

target_link_libraries(libjson PUBLIC
	libsys
)

target_link_libraries(libjson PRIVATE
	libsimple
	libmath
)

target_include_directories(libjson PUBLIC ${libjson_includes})

add_library(libjson_static STATIC ${libjson_sources})

set_target_properties(libjson_static PROPERTIES
	OUTPUT_NAME json
)

target_link_libraries(libjson_static PUBLIC
	libsys_headers
)

target_link_libraries(libjson_static PRIVATE
	libsimple_headers
	libmath_headers
)

target_include_directories(libjson_static PUBLIC ${libjson_includes})

add_subdirectory(tests)

sanitize_targets(
	libjson
	libjson_static
)
