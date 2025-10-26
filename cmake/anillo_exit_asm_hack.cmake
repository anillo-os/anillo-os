# `exit-asm.o` is a hack to prevent ld64 from erroring due to not linking to libSystem (because it thinks this is actually macOS we're building for)
# it works because ld64 specifically ignored the requirement for linking to libSystem when there is an input file called `exit-asm.o`
function(anillo_exit_asm_hack)
	if (NOT TARGET anillo_exit_asm_hack)
		add_library(anillo_exit_asm_hack OBJECT "${CMAKE_SOURCE_DIR}/cmake/exit-asm.c")
		add_custom_command(
			OUTPUT "${CMAKE_BINARY_DIR}/exit-asm.o"
			COMMAND cp $<TARGET_OBJECTS:anillo_exit_asm_hack> "${CMAKE_BINARY_DIR}/exit-asm.o"
			DEPENDS $<TARGET_OBJECTS:anillo_exit_asm_hack>
		)
	endif()
endfunction()
