project(libspooky C)

set(libspooky_sources
	src/data.c
	src/deserializer.c
	src/function.c
	src/interface.c
	src/invocation.c
	src/objects.c
	src/proxy.c
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
add_library(libspooky_dymple STATIC ${libspooky_sources})

target_compile_definitions(libspooky_dymple PRIVATE
	BUILDING_DYMPLE=1
)

set_target_properties(libspooky_static PROPERTIES
	OUTPUT_NAME spooky
)
set_target_properties(libspooky_dymple PROPERTIES
	OUTPUT_NAME spooky_dymple
)

target_link_libraries(libspooky PUBLIC
	libsys_core
	libeve
)

target_link_libraries(libspooky_static PUBLIC
	libsys_core_static
	libeve_static
)
target_link_libraries(libspooky_dymple PUBLIC
	libsys_core_dymple
	libeve_dymple
)

target_link_libraries(libspooky PRIVATE
	libsimple
)

target_link_libraries(libspooky_static PRIVATE
	libsimple_static
)
target_link_libraries(libspooky_dymple PRIVATE
	libsimple_static
)

target_include_directories(libspooky PUBLIC ${libspooky_includes})
target_include_directories(libspooky_static PUBLIC ${libspooky_includes})
target_include_directories(libspooky_dymple PUBLIC ${libspooky_includes})

add_library(libspooky_headers INTERFACE)
target_include_directories(libspooky_headers INTERFACE ${libspooky_includes})
target_link_libraries(libspooky_headers INTERFACE
	libeve_headers
)
