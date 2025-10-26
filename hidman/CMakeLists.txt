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

add_to_ramdisk(
	SOURCE resources/org.anillo.hidman.json5
	DESTINATION /sys/config/sysman/org.anillo.hidman.json5
)

target_link_libraries(hidman PUBLIC
	libc
	libsys
	libsimple
	libeve
	libspooky
)

sanitize_targets(
	hidman
)
