project(uefi-bootstrap)

add_compile_options(
	-target ${ANILLO_ARCH}-unknown-windows
	-fshort-wchar
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
string(REPLACE "-Wl,-m,elf_x86_64" "" CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS}")

include_directories(
	../../../include

	# TODO
	"${CMAKE_SOURCE_DIR}/build/x86_64/kernel/include"
)

add_executable(uefi-bootstrap
	main.c
	wrappers.c
	../../libk/libk.c
)

set_target_properties(uefi-bootstrap PROPERTIES
	OUTPUT_NAME "ferro-bootstrap"
	SUFFIX ".efi"
)
