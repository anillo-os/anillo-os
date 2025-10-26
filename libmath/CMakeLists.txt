project(libmath C)

set(libmath_sources
	src/pow.c
)

set(libmath_includes
	include
)

add_anillo_dylib(libmath
	RAMDISK
	INSTALL_PATH /sys/lib/libmath.dylib
	SOURCES
		${libmath_sources}
)

target_link_libraries(libmath PRIVATE
	libsimple
)

target_include_directories(libmath PUBLIC ${libmath_includes})

add_library(libmath_static STATIC ${libmath_sources})

set_target_properties(libmath_static PROPERTIES
	OUTPUT_NAME math
)

target_link_libraries(libmath_static PRIVATE
	libsimple_headers
)

target_include_directories(libmath_static PUBLIC ${libmath_includes})

add_library(libmath_headers INTERFACE)
target_include_directories(libmath_headers INTERFACE ${libmath_includes})
target_link_libraries(libmath_headers INTERFACE libsimple_headers)

sanitize_targets(
	libmath
)
