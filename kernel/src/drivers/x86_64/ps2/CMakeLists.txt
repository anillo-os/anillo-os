project(ferro-drivers-x86_64-ps2 C)

add_library(ferro_drivers_x86_64_ps2 STATIC
	keyboard.c
)

target_link_libraries(ferro_drivers_x86_64_ps2 PRIVATE
	libsimple_static_kernel
	ferro_drivers
)
