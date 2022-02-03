project(ferro-syscalls-x86_64 C)

add_library(ferro_syscalls_x86_64 STATIC
	thread_set_fs.c
	thread_set_gs.c
)

add_dependencies(ferro_syscalls_x86_64
	generate_syscalls
)

target_link_libraries(ferro_syscalls_x86_64 PRIVATE
	libsimple_static_kernel
	libsimple_static_compat
)
