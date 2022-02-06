project(ferro-userspace C)

add_library(ferro_userspace STATIC
	threads.c
	loader.c
	entry.c
	processes.c
	futex.c
)

add_subdirectory("${ANILLO_ARCH}")

target_link_libraries(ferro_userspace PUBLIC
	ferro_userspace_${ANILLO_ARCH}
)

target_link_libraries(ferro_userspace PRIVATE
	libsimple_static_kernel
	libsimple_static_compat

	libmacho
)
