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

add_to_ramdisk(
	SOURCE resources/org.anillo.conman.json5
	DESTINATION /sys/config/sysman/org.anillo.conman.json5
)

target_link_libraries(conman PUBLIC
	libc
	libsys
	libsimple
	libeve
	libspooky
)

sanitize_targets(
	conman
)
