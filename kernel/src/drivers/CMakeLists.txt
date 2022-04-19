project(ferro-drivers C ASM)

add_library(ferro_drivers STATIC
	init.c

	pci.c
)

target_link_libraries(ferro_drivers PRIVATE
	libsimple_static_kernel
	libsimple_static_compat
)
