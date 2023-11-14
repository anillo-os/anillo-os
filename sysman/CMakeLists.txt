project(sysman C)

spooky_gen(conman_client src/con.spky
	HEADER "${CMAKE_CURRENT_BINARY_DIR}/public/libconman/libconman.h"
)
spooky_gen(conman_server src/con.spky SERVER)

add_library(libconman_static STATIC
	"${CMAKE_CURRENT_BINARY_DIR}/con.client.c"
)

target_include_directories(libconman_static PUBLIC
	"${CMAKE_CURRENT_BINARY_DIR}/public"
)

target_include_directories(libconman_static PRIVATE
	"${CMAKE_CURRENT_BINARY_DIR}/public/libconman"
)

set_target_properties(libconman_static PROPERTIES
	OUTPUT_NAME conman
)

target_link_libraries(libconman_static PUBLIC
	libsys_headers
	libeve_headers
)

target_link_libraries(libconman_static PRIVATE
	libsimple_headers
	libspooky_headers
)

add_link_options(
	-fvisibility=hidden
)

add_anillo_executable(sysman
	RAMDISK
	INSTALL_PATH /sys/sysman/sysman
	SOURCES
		src/sysman.c
		src/conman.c
		"${CMAKE_CURRENT_BINARY_DIR}/con.server.c"
)

target_include_directories(sysman PRIVATE
	"${CMAKE_CURRENT_BINARY_DIR}"
)

target_link_libraries(sysman PRIVATE
	libsys_core_static
	libsys_support_static
	libsimple_static
	libeve_static
	libspooky_static
	libstackchk_stub_static
	libvfsman_static
)

target_link_options(sysman PRIVATE
	-Wl,-static
)

add_anillo_executable(tinysh
	RAMDISK
	INSTALL_PATH /sys/sysman/tinysh
	SOURCES
		src/tinysh.c
)

target_include_directories(tinysh PRIVATE
	"${CMAKE_CURRENT_BINARY_DIR}"
)

target_link_libraries(tinysh PUBLIC
	libc
	libsys
	libsimple
	libeve
	libspooky
)
