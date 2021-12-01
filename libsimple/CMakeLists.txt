project(libsimple C)

set(libsimple_sources
	src/libsimple.c
	src/ghmap.c
)

set(libsimple_includes
	include
)

add_anillo_dylib(libsimple
	RAMDISK
	INSTALL_PATH /sys/lib/libsimple.dylib
	SOURCES
		${libsimple_sources}
)

add_library(libsimple_static STATIC ${libsimple_sources})
add_library(libsimple_static_kernel STATIC ${libsimple_sources})
add_library(libsimple_static_compat INTERFACE)

target_link_options(libsimple_static_compat INTERFACE
	-Wl,-alias,_simple_memset,_memset
	-Wl,-alias,_simple_memcpy,_memcpy
	-Wl,-alias,_simple_bzero,_bzero
	-Wl,-alias,_simple_bzero,___bzero
)

target_link_libraries(libsimple PRIVATE
	libsimple_static_compat
)

set(libsimple_kernel_compile_options
	-fno-plt
	-fno-pic
	-mcmodel=large

	-fno-stack-protector
	-fno-stack-check
	-mno-red-zone
	-mno-implicit-float
)

target_compile_options(libsimple_static_kernel PRIVATE ${libsimple_kernel_compile_options})

set_target_properties(libsimple_static PROPERTIES
	OUTPUT_NAME simple
)

set_target_properties(libsimple_static_kernel PROPERTIES
	OUTPUT_NAME simple_kernel
)

target_include_directories(libsimple PUBLIC ${libsimple_includes})
target_include_directories(libsimple_static PUBLIC ${libsimple_includes})
target_include_directories(libsimple_static_kernel PUBLIC ${libsimple_includes})

target_link_libraries(libsimple PUBLIC ferro_headers_base)
target_link_libraries(libsimple_static PUBLIC ferro_headers_base)
target_link_libraries(libsimple_static_kernel PUBLIC ferro_headers_base)

add_library(libsimple_headers INTERFACE)
target_include_directories(libsimple_headers INTERFACE ${libsimple_includes})
target_link_libraries(libsimple_headers INTERFACE ferro_headers_base)
