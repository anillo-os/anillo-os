project(ferro-drivers-x86_64 C)

add_subdirectory(ps2)

add_library(ferro_drivers_x86_64 STATIC
	pci.c
)

target_link_libraries(ferro_drivers_x86_64 PRIVATE
	libsimple_static_kernel
	ferro_drivers_x86_64_ps2
)
