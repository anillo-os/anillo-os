project(ferro C ASM)

find_program(LD_LLD_PATH ld.lld REQUIRED)

add_link_options(
	-static
	-pie

	-target "${ANILLO_TARGET_TRIPLE_KERNEL}"

	-fuse-ld=lld
	"--ld-path=${LD_LLD_PATH}"
)

add_compile_options(
	-fno-plt
	-fno-pic
	-mcmodel=large

	-fno-stack-protector
	-fno-stack-check
	-mno-red-zone

	-target "${ANILLO_TARGET_TRIPLE_KERNEL}"
)

add_compile_definitions(
	FERRO=1
	FERRO_ELF=1
)

if (FERRO_UBSAN AND FERRO_KASAN)
	add_compile_options(
		-fsanitize=undefined,kernel-address
	)
elseif(FERRO_UBSAN)
	add_compile_options(
		-fsanitize=undefined
	)
elseif(FERRO_KASAN)
	add_compile_options(
		-fsanitize=kernel-address
	)
endif()

if(FERRO_KASAN)
	# asan shadow base = 0xfffffe8000000000
	# asan shadow scale = 8
	# mapping offset = asan shadow base - (kernel half start / asan shadow scale)
	#                = 0xe0000e8000000000
	add_compile_options(
		"SHELL:-mllvm -asan-mapping-offset=0xe0000e8000000000"
		"SHELL:-mllvm -asan-globals=false"
	)

	add_compile_definitions(
		FERRO_KASAN=1
	)
endif()

set(CMAKE_EXE_LINKER_FLAGS "")

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
add_subdirectory(src/mm)
add_subdirectory(src/kasan)

add_executable(ferro
	src/dummy.c
	src/testing.c
)

target_link_libraries(ferro PUBLIC
	ferro_core
	ferro_mm
	ferro_gdbstub
	ferro_ubsan
	ferro_userspace
	ferro_syscalls
	ferro_drivers
	ferro_kasan
	compiler-rt-builtins
)

target_link_libraries(ferro PRIVATE
	libsimple_static_kernel
	libsimple_static_kernel_compat
)

#target_link_options(ferro PUBLIC
#	-Wl,-dead_strip
#	-Wl,-e,_ferro_entry
#	-Wl,-pagezero_size,0
#	-Wl,-image_base,0xffff800000000000
#	-Wl,-execute
#)

target_link_options(ferro PUBLIC
	#-Wl,--strip-discarded
	-Wl,-e,ferro_entry
	-Wl,--image-base=0xffff800000000000
	-Wl,-z,separate-loadable-segments
)

add_library(ferro_headers_base INTERFACE)
target_include_directories(ferro_headers_base INTERFACE include)

add_library(ferro_headers_uapi INTERFACE)
target_include_directories(ferro_headers_uapi INTERFACE user-include)
target_link_libraries(ferro_headers_uapi INTERFACE ferro_headers_base)
