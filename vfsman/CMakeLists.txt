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

add_dependencies(libvfs_static vfsman_client)

add_library(libvfsman_static STATIC
	src/server/ramdisk.c
	src/server/vfsman.c
	src/server/server.c
	"${CMAKE_CURRENT_BINARY_DIR}/vfs.server.c"
)

set_target_properties(libvfsman_static PROPERTIES
	OUTPUT_NAME vfsman
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

target_include_directories(libvfsman_static PUBLIC
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

target_link_libraries(libvfsman_static PRIVATE
	libsys_core_static
	libsimple_static
	libeve_static
	libspooky_static
)

add_library(libvfs_headers INTERFACE)
target_include_directories(libvfs_headers INTERFACE include/public)
target_link_libraries(libvfs_headers INTERFACE libsys_headers)

sanitize_targets(
	libvfs_static
	libvfsman_static
)
