project(sysman C)

add_compile_options(
	-fPIC
)

add_link_options(
	-pie
	-fvisibility=hidden
	-Wl,--exclude-libs,ALL
	-Wl,--dynamic-linker=/sys/support/dylink
)

add_executable(sysman
	src/sysman.c
)

target_link_libraries(sysman PUBLIC
	libc
)

add_to_ramdisk(
	TARGET sysman
	DESTINATION /sys/sysman/sysman
)
