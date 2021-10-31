project(libc C)

add_library(libc SHARED
	src/stdlib.c
)

set_target_properties(libc PROPERTIES
	OUTPUT_NAME c
)

target_link_libraries(libc PRIVATE
	libsys
)

target_include_directories(libc PUBLIC
	include
)

add_to_ramdisk(
	TARGET libc
	DESTINATION /sys/lib/libc.so
)
