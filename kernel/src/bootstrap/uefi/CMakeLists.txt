project(uefi-bootstrap)

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

# remove a troublesome linker flag
#
# it's easier to just remove it here for the UEFI target rather than removing it from the toolchain file
# because then we'd have to manually add it to all other targets
if ("${ANILLO_ARCH}" STREQUAL "aarch64")
	string(REPLACE "-Wl,-m,aarch64linux" "" CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS}")
elseif ("${ANILLO_ARCH}" STREQUAL "x86_64")
	string(REPLACE "-Wl,-m,elf_x86_64" "" CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS}")
endif()

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
)

target_compile_definitions(uefi-bootstrap PRIVATE
	LIBSIMPLE_COMPAT=1
)

set_target_properties(uefi-bootstrap PROPERTIES
	OUTPUT_NAME "ferro-bootstrap"
	SUFFIX ".efi"
)
