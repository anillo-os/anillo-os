project(uefi-bootstrap)

if (ANILLO_ARCH STREQUAL "x86_64")
	set(CMAKE_C_FLAGS "-ffreestanding -nostdlib -target ${ANILLO_TARGET_TRIPLE} -U__APPLE__ -D__ANILLO__=1")
	set(CMAKE_ASM_FLAGS "-nostdlib -target ${ANILLO_TARGET_TRIPLE}")
elseif(ANILLO_ARCH STREQUAL "aarch64")
	set(CMAKE_C_FLAGS "-ffreestanding -mno-implicit-float -nostdlib -target ${ANILLO_TARGET_TRIPLE} -U__APPLE__ -D__ANILLO__=1")
	set(CMAKE_ASM_FLAGS "-nostdlib -target ${ANILLO_TARGET_TRIPLE}")
endif()

add_compile_options(
	-target ${ANILLO_ARCH}-unknown-windows
	-fshort-wchar

	-fno-stack-protector
	-fno-stack-check
	-mno-red-zone
	-mno-implicit-float
)

add_link_options(
	-target ${ANILLO_ARCH}-unknown-windows
	-Wl,-entry:efi_main
	-Wl,-subsystem:efi_application
)

find_program(LLD_PATH lld-link REQUIRED)

set(CMAKE_EXE_LINKER_FLAGS "-fuse-ld=lld --ld-path=${LLD_PATH}")

include_directories(
	../../../include
	../../../kernel-include
	../../../../libsimple/include

	"${CMAKE_BINARY_DIR}/kernel/include"
)

add_executable(uefi-bootstrap
	main.c
	wrappers.c
	../../../../libsimple/src/libsimple.c
	../../../../libsimple/src/generic/memcpy.c
	../../../../libsimple/src/generic/memset.c
	../../../../libsimple/src/generic/memmove.c
)

target_compile_definitions(uefi-bootstrap PRIVATE
	LIBSIMPLE_UEFI_COMPAT=1
	FERRO_ELF=1
)

target_link_libraries(uefi-bootstrap PUBLIC
	libmacho
	libelf
)

set_target_properties(uefi-bootstrap PROPERTIES
	OUTPUT_NAME "ferro-bootstrap"
	SUFFIX ".efi"
)
