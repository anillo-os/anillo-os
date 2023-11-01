project(ferro-kasan C)

add_compile_options(
	-fno-sanitize=kernel-address
)

add_library(ferro_kasan STATIC
	kasan.c
)

target_link_libraries(ferro_kasan PRIVATE
	libsimple_static_kernel
)
