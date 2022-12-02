project(sysman C)

spooky_gen(sysman_test_client src/test.spky)
spooky_gen(sysman_test_server src/test.spky SERVER)

add_link_options(
	-fvisibility=hidden
)

add_anillo_executable(sysman
	RAMDISK
	INSTALL_PATH /sys/sysman/sysman
	SOURCES
		src/sysman.c
		"${CMAKE_CURRENT_BINARY_DIR}/test.server.c"
)

target_include_directories(sysman PRIVATE
	"${CMAKE_CURRENT_BINARY_DIR}"
)

target_link_libraries(sysman PUBLIC
	libc
	libsys
	libsimple
	libeve
	libspooky
)

add_anillo_executable(tinysh
	RAMDISK
	INSTALL_PATH /sys/sysman/tinysh
	SOURCES
		src/tinysh.c
		"${CMAKE_CURRENT_BINARY_DIR}/test.client.c"
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

add_dependencies(sysman sysman_test_server)
add_dependencies(tinysh sysman_test_client)
