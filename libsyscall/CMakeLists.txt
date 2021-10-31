project(libsyscall C ASM)

add_compile_options(
	-fvisibility=hidden
	-fPIC
)

add_library(libsyscall STATIC
	src/${ANILLO_ARCH}/syscall-invoke.S

	"${CMAKE_BINARY_DIR}/src/gen/libsyscall/syscall-wrappers.c"
)

target_include_directories(libsyscall PUBLIC
	"${CMAKE_CURRENT_SOURCE_DIR}/../kernel/include"
	"${CMAKE_CURRENT_SOURCE_DIR}/../kernel/user-include"
)

set_target_properties(libsyscall PROPERTIES
	OUTPUT_NAME syscall
)

add_dependencies(libsyscall
	generate_syscalls
)
