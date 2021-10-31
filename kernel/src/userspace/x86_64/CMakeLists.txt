project(ferro-userspace-x86_64 C ASM)

add_library(ferro_userspace_x86_64 STATIC
	syscalls.c
	threads.c

	syscall-invoker.S
	thread-jumper.S
)

add_dependencies(ferro_userspace_x86_64
	generate_offsets_header
	generate_syscalls
)

target_link_libraries(ferro_userspace_x86_64 PRIVATE
	libsimple_static_compat_kernel
)
