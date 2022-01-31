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

target_link_libraries(sysman PUBLIC
	libc
	libsys
)
