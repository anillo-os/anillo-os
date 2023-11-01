project(ferro-mm-x86_64 C)

add_library(ferro_mm_x86_64 STATIC
	paging.c
)

target_link_libraries(ferro_mm_x86_64 PRIVATE
	libsimple_static_kernel
)
