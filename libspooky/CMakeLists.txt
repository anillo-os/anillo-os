project(libspooky C)

set(libspooky_sources
	src/data.c
	src/deserializer.c
	src/function.c
	src/interface.c
	src/invocation.c
	src/objects.c
	src/serializer.c
	src/structure.c
	src/types.c
)

set(libspooky_includes
	include
)

add_anillo_dylib(libspooky
	RAMDISK
	INSTALL_PATH /sys/lib/libspooky.dylib
	SOURCES
		${libspooky_sources}
)

add_library(libspooky_static STATIC ${libspooky_sources})

set_target_properties(libspooky_static PROPERTIES
	OUTPUT_NAME spooky
)

target_link_libraries(libspooky PUBLIC
	libsys
	libeve
)

target_link_libraries(libspooky_static PUBLIC
	libsys_static
	libeve_static
)

target_link_libraries(libspooky PRIVATE
	libsimple
)

target_link_libraries(libspooky_static PRIVATE
	libsimple_static
)

target_include_directories(libspooky PUBLIC ${libspooky_includes})
target_include_directories(libspooky_static PUBLIC ${libspooky_includes})
