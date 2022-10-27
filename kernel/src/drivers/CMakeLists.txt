project(ferro-drivers C)

add_library(ferro_drivers STATIC
	init.c
	pci.c
	keyboard.c
	mouse.c
)

add_subdirectory("${ANILLO_ARCH}")

target_link_libraries(ferro_drivers PUBLIC
	ferro_drivers_${ANILLO_ARCH}
)

target_link_libraries(ferro_drivers PRIVATE
	libsimple_static_kernel
)
