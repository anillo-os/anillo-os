project(ferro-ubsan C)

add_library(ferro_ubsan STATIC
	ubsan.c
)

target_link_libraries(ferro_ubsan PRIVATE
	libsimple_static_kernel
)
