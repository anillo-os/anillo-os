project(displayman C)

include_directories(
	include
)

add_link_options(
	-fvisibility=hidden
)

add_anillo_executable(displayman
	RAMDISK
	INSTALL_PATH /sys/displayman/displayman
	SOURCES
		src/displayman.c
)

add_to_ramdisk(
	SOURCE resources/org.anillo.displayman.json5
	DESTINATION /sys/config/sysman/org.anillo.displayman.json5
)

target_link_libraries(displayman PUBLIC
	libc
	libsys
	libsimple
	libeve
	libspooky
)

sanitize_targets(
	displayman
)
