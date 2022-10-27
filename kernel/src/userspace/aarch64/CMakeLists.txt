project(ferro-userspace-aarch64 C ASM)

add_library(ferro_userspace_aarch64 STATIC
	syscalls.c
	threads.c

	syscall-invoker.S
	thread-jumper.S
)

add_dependencies(ferro_userspace_aarch64
	generate_offsets_header
	generate_syscalls
)

target_link_libraries(ferro_userspace_aarch64 PRIVATE
	libsimple_static_kernel
)
