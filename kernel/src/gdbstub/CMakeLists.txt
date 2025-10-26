project(ferro-gdbstub C)

include_directories(
	"${CMAKE_CURRENT_BINARY_DIR}/include"
)

encode_file(
	"../../src/gdbstub/${ANILLO_ARCH}/target.xml"
	"include/gen/ferro/gdbstub/target.xml.h"
	target_xml_data
	generate_target_xml_header
)

add_library(ferro_gdbstub STATIC
	main.c
	malloc.c
	packet-buffer.c
)

add_dependencies(ferro_gdbstub generate_target_xml_header)

add_subdirectory("${ANILLO_ARCH}")

target_link_libraries(ferro_gdbstub PUBLIC
	ferro_gdbstub_${ANILLO_ARCH}
)

target_link_libraries(ferro_gdbstub PRIVATE
	libsimple_static_kernel
)
