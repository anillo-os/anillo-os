project(sysman C)

add_link_options(
	-fvisibility=hidden
)

add_anillo_executable(sysman
	RAMDISK
	INSTALL_PATH /sys/sysman/sysman
	SOURCES
		src/sysman.c
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
