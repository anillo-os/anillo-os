project(ferro C ASM)

add_link_options(
	-static
	-pie
)

add_compile_options(
	-fno-plt
	-fno-pic
	-mcmodel=large

	-fno-stack-protector
	-fno-stack-check
	-mno-red-zone
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
add_subdirectory(src/drivers)

add_executable(ferro
	src/dummy.c
	src/testing.c
)

target_link_libraries(ferro PUBLIC
	ferro_core
	ferro_gdbstub
	ferro_ubsan
	ferro_userspace
	ferro_syscalls
	ferro_drivers
	compiler-rt-builtins
)

target_link_libraries(ferro PRIVATE
	libsimple_static_kernel
	libsimple_static_compat
)

target_link_options(ferro PUBLIC
	-Wl,-dead_strip
	-Wl,-e,_ferro_entry
	-Wl,-pagezero_size,0
	-Wl,-image_base,0xffff800000000000
	-Wl,-execute
)

add_library(ferro_headers_base INTERFACE)
target_include_directories(ferro_headers_base INTERFACE include)

add_library(ferro_headers_uapi INTERFACE)
target_include_directories(ferro_headers_uapi INTERFACE user-include)
target_link_libraries(ferro_headers_uapi INTERFACE ferro_headers_base)
