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

target_link_libraries(libeve PUBLIC
	libsys

	# this is cheating (only libsys is supposed to link to libsyscall),
	# but this allows us to do some pretty cool shenanigans with futex-based libsys synchronization objects
	libsyscall
)

target_link_libraries(libeve PRIVATE
	libsimple
)

target_include_directories(libeve PUBLIC ${libeve_includes})
