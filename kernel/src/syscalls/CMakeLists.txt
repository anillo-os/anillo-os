project(ferro-syscalls C)

add_subdirectory("${ANILLO_ARCH}")

add_library(ferro_syscalls STATIC
	syscall-table.c

	exit.c
	log.c
	page_allocate.c
	page_free.c
	page_protect.c
	page_shared.c
	page_translate.c
	thread_create.c
	thread_id.c
	thread_kill.c
	thread_misc.c
	thread_suspend.c
	thread_resume.c
	thread_signal.c
	futex_wait.c
	futex_wake.c
	futex_associate.c
	process_create.c
	process_id.c
	process_kill.c
	process_suspend.c
	process_resume.c
	channels.c
	monitors.c
	constants.c
)

add_dependencies(ferro_syscalls
	generate_syscalls
)

target_link_libraries(ferro_syscalls PRIVATE
	libsimple_static_kernel
	ferro_syscalls_${ANILLO_ARCH}
)
