project(ferro-drivers-aarch64 C)

add_library(ferro_drivers_aarch64 STATIC
	pci.c
)

target_link_libraries(ferro_drivers_aarch64 PRIVATE
	libsimple_static_kernel
)
