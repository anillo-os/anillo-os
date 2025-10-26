project(libpci C)

set(libpci_sources
	src/libpci.c
	src/objects.c
)

set(libpci_includes
	include
)

add_anillo_dylib(libpci
	RAMDISK
	INSTALL_PATH /sys/lib/libpci.dylib
	SOURCES
		${libpci_sources}
)

target_link_libraries(libpci PUBLIC
	libsys
	libeve
)

target_link_libraries(libpci PRIVATE
	libsimple
)

target_include_directories(libpci PUBLIC ${libpci_includes})

sanitize_targets(
	libpci
)
