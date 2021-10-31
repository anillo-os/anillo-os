project(libsys C)

set(libsys_sources
	src/abort.c
	src/console.c
	src/files.c
	src/format.c
	src/general.c
	src/locks.c
	src/mempool.c
	src/objects.c
	src/pages.c
	src/streams.c
)

set(libsys_libs
	libsyscall
)

set(libsys_includes
	include
	"${CMAKE_CURRENT_SOURCE_DIR}/../kernel/include"
	"${CMAKE_CURRENT_SOURCE_DIR}/../kernel/user-include"
)

add_library(libsys SHARED ${libsys_sources})
add_library(libsys_static STATIC ${libsys_sources})

set_target_properties(libsys libsys_static PROPERTIES
	OUTPUT_NAME sys
)

target_link_libraries(libsys PRIVATE
	${libsys_libs}
	libsimple
)
target_link_libraries(libsys_static PRIVATE
	${libsys_libs}
	libsimple_static
)

target_include_directories(libsys PUBLIC ${libsys_includes})
target_include_directories(libsys_static PUBLIC ${libsys_includes})

add_to_ramdisk(
	TARGET libsys
	DESTINATION /sys/lib/libsys.so
)
