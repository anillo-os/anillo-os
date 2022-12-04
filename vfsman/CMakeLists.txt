project(vfsman C)

spooky_gen(vfsman_client src/vfs.spky)
spooky_gen(vfsman_server src/vfs.spky SERVER)

set(libvfs_sources
	src/client/libvfs.c
)

set(libvfs_includes
	include/public
)

add_library(libvfs_static STATIC ${libvfs_sources})

set_target_properties(libvfs_static PROPERTIES
	OUTPUT_NAME vfs
)

add_anillo_executable(vfsman
	RAMDISK
	INSTALL_PATH /sys/vfsman/vfsman
	SOURCES
		src/server/ramdisk.c
		src/server/vfsman.c
		"${CMAKE_CURRENT_BINARY_DIR}/vfs.server.c"
)

target_include_directories(libvfs_static PUBLIC
	include/public
	include/private
)

target_include_directories(libvfs_static PRIVATE
	"${CMAKE_CURRENT_BINARY_DIR}"
	include/public
	include/private
)

target_include_directories(libvfs_static PRIVATE
	"${CMAKE_CURRENT_BINARY_DIR}"
	include/public
	include/private
)

target_include_directories(vfsman PRIVATE
	"${CMAKE_CURRENT_BINARY_DIR}"
	include/public
	include/private
)

target_link_libraries(libvfs_static PUBLIC
	libsys_headers
	libeve_headers
)

target_link_libraries(libvfs_static PRIVATE
	libsimple_headers
	libspooky_headers
)

target_link_libraries(vfsman PRIVATE
	libsys_core_static
	libsys_support_static
	libsimple_static
	libeve_static
	libspooky_static
	libstackchk_stub_static
)

target_link_options(vfsman PRIVATE
	-Wl,-static
)
