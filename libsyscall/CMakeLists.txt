project(libsyscall C ASM)

add_compile_options(
	-fvisibility=hidden
	-fPIC
)

add_library(libsyscall STATIC
	src/${ANILLO_ARCH}/syscall-invoke.S

	"${CMAKE_BINARY_DIR}/src/gen/libsyscall/syscall-wrappers.c"
)

set_target_properties(libsyscall PROPERTIES
	OUTPUT_NAME syscall
)

add_dependencies(libsyscall
	generate_syscalls
)

target_link_libraries(libsyscall PUBLIC ferro_headers_uapi)
