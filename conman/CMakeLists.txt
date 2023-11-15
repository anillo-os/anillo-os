project(conman C)

include_directories(
	include
)

add_link_options(
	-fvisibility=hidden
)

add_anillo_executable(conman
	RAMDISK
	INSTALL_PATH /sys/conman/conman
	SOURCES
		src/conman.c
)

target_link_libraries(conman PUBLIC
	libc
	libsys
	libsimple
	libeve
	libspooky
)
