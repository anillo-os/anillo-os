project(libeve C)

set(libeve_sources
	src/channel.c
	src/item.c
	src/loop.c
	src/objects.c
	src/server-channel.c
)

set(libeve_includes
	include
)

add_anillo_dylib(libeve
	RAMDISK
	INSTALL_PATH /sys/lib/libeve.dylib
	SOURCES
		${libeve_sources}
)

add_library(libeve_static STATIC ${libeve_sources})
add_library(libeve_dymple STATIC ${libeve_sources})

target_compile_definitions(libeve_dymple PRIVATE
	BUILDING_DYMPLE=1
)

set_target_properties(libeve_static PROPERTIES
	OUTPUT_NAME eve
)

set_target_properties(libeve_dymple PROPERTIES
	OUTPUT_NAME eve_dymple
)

target_link_libraries(libeve PUBLIC
	libsys_core

	# this is cheating (only libsys is supposed to link to libsyscall),
	# but this allows us to do some pretty cool shenanigans with futex-based libsys synchronization objects
	libsyscall
)

target_link_libraries(libeve_static PUBLIC
	libsys_core_static
	libsyscall
)

target_link_libraries(libeve_dymple PUBLIC
	libsys_core_dymple
	libsyscall
)

target_link_libraries(libeve PRIVATE
	libsimple
)

target_link_libraries(libeve_static PRIVATE
	libsimple_static
)

target_link_libraries(libeve_dymple PRIVATE
	libsimple_static
)

target_include_directories(libeve PUBLIC ${libeve_includes})
target_include_directories(libeve_static PUBLIC ${libeve_includes})
target_include_directories(libeve_dymple PUBLIC ${libeve_includes})

add_library(libeve_headers INTERFACE)
target_include_directories(libeve_headers INTERFACE ${libeve_includes})
target_link_libraries(libeve_headers INTERFACE
	libsys_headers
)
