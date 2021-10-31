project(dylink C)

add_compile_options(
	-fPIC
)

add_link_options(
	-static
	-pie
	-fvisibility=hidden
	-Wl,--exclude-libs,ALL
)

add_executable(dylink
	src/dylink.c
)

target_link_libraries(dylink PUBLIC
	libsys_static
	libsimple_static_compat
	libelf
)

add_to_ramdisk(
	TARGET dylink
	DESTINATION /sys/support/dylink
)
