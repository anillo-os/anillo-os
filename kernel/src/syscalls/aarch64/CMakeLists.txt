project(ferro-syscalls-aarch64 C)

add_library(ferro_syscalls_aarch64 STATIC
	thread_set_tpidr.c
)

add_dependencies(ferro_syscalls_aarch64
	generate_syscalls
)

target_link_libraries(ferro_syscalls_aarch64 PRIVATE
	libsimple_static_kernel
)
