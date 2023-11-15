project(hidman C)

include_directories(
	include
)

add_link_options(
	-fvisibility=hidden
)

add_anillo_executable(hidman
	RAMDISK
	INSTALL_PATH /sys/hidman/hidman
	SOURCES
		src/hidman.c
)

target_link_libraries(hidman PUBLIC
	libc
	libsys
	libsimple
	libeve
	libspooky
)
