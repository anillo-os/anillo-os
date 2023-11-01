project(ferro-mm-aarch64 C)

add_library(ferro_mm_aarch64 STATIC
	paging.c
)

target_link_libraries(ferro_mm_aarch64 PRIVATE
	libsimple_static_kernel
)
