project(ferro-syscalls-aarch64 C)

add_library(ferro_syscalls_aarch64 STATIC
	${CMAKE_SOURCE_DIR}/cmake/dummy.c
)

add_dependencies(ferro_syscalls_aarch64
	generate_syscalls
)

target_link_libraries(ferro_syscalls_aarch64 PRIVATE
	libsimple_static_kernel
	libsimple_static_compat
)
