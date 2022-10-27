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

target_link_libraries(libspooky PUBLIC
	libsys
	libeve
)

target_link_libraries(libspooky PRIVATE
	libsimple
)

target_include_directories(libspooky PUBLIC ${libspooky_includes})
