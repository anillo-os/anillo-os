project(vfsman C)

set(libvfs_sources
	src/client/libvfs.c
)

set(libvfs_includes
	include/public
)

add_anillo_dylib(libvfs
	RAMDISK
	INSTALL_PATH /sys/lib/libvfs.dylib
	SOURCES
		${libvfs_sources}
)

add_anillo_executable(vfsman
	RAMDISK
	INSTALL_PATH /sys/vfsman/vfsman
	SOURCES
		src/server/vfsman.c
)

target_include_directories(libvfs PUBLIC
	include/public
)

target_include_directories(libvfs PRIVATE
	"${CMAKE_CURRENT_BINARY_DIR}"
	include/public
	include/private
)

target_include_directories(vfsman PRIVATE
	"${CMAKE_CURRENT_BINARY_DIR}"
	include/public
	include/private
)

target_link_libraries(libvfs PUBLIC
	libsys
	libeve
)

target_link_libraries(libvfs PRIVATE
	libsimple
	libspooky
)

target_link_libraries(vfsman PRIVATE
	libsys_static
	libsimple_static
	libeve_static
	libspooky_static
)

target_link_options(vfsman PRIVATE
	-Wl,-static
)
