project(ferro C ASM)

add_link_options(
	-static
	"-T${CMAKE_CURRENT_SOURCE_DIR}/scripts/kernel.lds"
	-Wl,--whole-archive
)

add_compile_options(
	-fno-plt
	-fno-pic
	-mcmodel=large

	-fno-stack-protector
	-fno-stack-check
	-mno-red-zone
	-mno-implicit-float
)

include_directories(
	include
	kernel-include

	"${CMAKE_CURRENT_BINARY_DIR}/include"
)

cmake_policy(SET CMP0116 NEW)

add_subdirectory(src/core)
add_subdirectory(src/gdbstub)
add_subdirectory(src/ubsan)
add_subdirectory(src/userspace)
add_subdirectory(src/syscalls)

add_executable(ferro
	src/dummy.c
)

target_link_libraries(ferro PUBLIC
	ferro_core
	ferro_gdbstub
	ferro_ubsan
	ferro_userspace
	ferro_syscalls
)

target_link_libraries(ferro PRIVATE
	"-Wl,--no-whole-archive $<TARGET_FILE:compiler-rt-builtins> -Wl,--whole-archive"
)

add_dependencies(ferro compiler-rt-builtins)
