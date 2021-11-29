project(ferro-syscalls C)

add_library(ferro_syscalls STATIC
	syscall-table.c

	exit.c
	log.c
	page_allocate_any.c
	page_free.c
	fd_open_special.c
	fd_close.c
	fd_read.c
	fd_write.c
	fd_copy_path.c
	fd_list_children.c
	fd_open.c
)

add_dependencies(ferro_syscalls
	generate_syscalls
)

target_link_libraries(ferro_syscalls PRIVATE
	libsimple_static_kernel
	libsimple_static_compat
)
