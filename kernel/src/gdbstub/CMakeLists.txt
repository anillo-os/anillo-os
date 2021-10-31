project(ferro-gdbstub C)

include_directories(
	"${CMAKE_CURRENT_BINARY_DIR}/include"
)

add_custom_command(
	OUTPUT
		"${CMAKE_CURRENT_BINARY_DIR}/include/gen/ferro/gdbstub/target.xml.h"
	COMMAND
		"${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/encode-file.py" "${CMAKE_CURRENT_SOURCE_DIR}/../../src/gdbstub/${ANILLO_ARCH}/target.xml" "${CMAKE_CURRENT_BINARY_DIR}/include/gen/ferro/gdbstub/target.xml.h" "target_xml_data"
	MAIN_DEPENDENCY
		"${CMAKE_CURRENT_SOURCE_DIR}/../../src/gdbstub/${ANILLO_ARCH}/target.xml"
	DEPENDS
		"${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/encode-file.py"
)

add_custom_target(generate_target_xml_header
	DEPENDS
		"${CMAKE_CURRENT_BINARY_DIR}/include/gen/ferro/gdbstub/target.xml.h"
)

add_library(ferro_gdbstub STATIC
	main.c
	malloc.c
	packet-buffer.c
)

add_subdirectory("${ANILLO_ARCH}")

target_link_libraries(ferro_gdbstub PUBLIC
	ferro_gdbstub_${ANILLO_ARCH}
)

target_link_libraries(ferro_gdbstub PRIVATE
	libsimple_static_compat_kernel
)
