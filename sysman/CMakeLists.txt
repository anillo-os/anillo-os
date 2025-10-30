project(sysman C)

spooky_gen(logman_client src/log.spky
	HEADER "${CMAKE_CURRENT_BINARY_DIR}/public/liblogman/liblogman.h"
)
spooky_gen(logman_server src/log.spky SERVER)

add_library(liblogman_static STATIC
	"${CMAKE_CURRENT_BINARY_DIR}/log.client.c"
)

target_include_directories(liblogman_static PUBLIC
	"${CMAKE_CURRENT_BINARY_DIR}/public"
)

target_include_directories(liblogman_static PRIVATE
	"${CMAKE_CURRENT_BINARY_DIR}/public/liblogman"
)

set_target_properties(liblogman_static PROPERTIES
	OUTPUT_NAME logman
)

target_link_libraries(liblogman_static PUBLIC
	libsys_headers
	libeve_headers
)

target_link_libraries(liblogman_static PRIVATE
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
		src/logman.c
		"${CMAKE_CURRENT_BINARY_DIR}/log.server.c"
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
	libjson_static
	libmath_static
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

sanitize_targets(
	liblogman_static
	sysman
	tinysh
)
