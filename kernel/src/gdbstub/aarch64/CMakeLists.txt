project(ferro-gdbstub-aarch64 C)

add_library(ferro_gdbstub_aarch64 STATIC
	registers.c
)

add_dependencies(ferro_gdbstub_aarch64
	generate_target_xml_header
)

target_link_libraries(ferro_gdbstub_aarch64 PRIVATE
	libsimple_static_compat_kernel
)
