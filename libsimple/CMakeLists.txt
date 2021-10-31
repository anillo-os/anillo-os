project(libsimple C)

set(libsimple_sources
	src/libsimple.c
	src/ghmap.c
)

set(libsimple_includes
	include

	"${CMAKE_CURRENT_SOURCE_DIR}/../kernel/include"
)

add_library(libsimple SHARED ${libsimple_sources})
add_library(libsimple_static STATIC ${libsimple_sources})

add_library(libsimple_static_compat STATIC ${libsimple_sources})

add_library(libsimple_static_kernel STATIC ${libsimple_sources})
add_library(libsimple_static_compat_kernel STATIC ${libsimple_sources})

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
target_compile_options(libsimple_static_compat_kernel PRIVATE ${libsimple_kernel_compile_options})

set_target_properties(libsimple libsimple_static PROPERTIES
	OUTPUT_NAME simple
)

set_target_properties(libsimple_static_compat PROPERTIES
	OUTPUT_NAME simple_compat
)

set_target_properties(libsimple_static_kernel PROPERTIES
	OUTPUT_NAME simple_kernel
)

set_target_properties(libsimple_static_compat_kernel PROPERTIES
	OUTPUT_NAME simple_compat_kernel
)

target_compile_definitions(libsimple_static_compat PRIVATE
	LIBSIMPLE_COMPAT=1
)

target_compile_definitions(libsimple_static_compat_kernel PRIVATE
	LIBSIMPLE_COMPAT=1
)

target_include_directories(libsimple PUBLIC ${libsimple_includes})
target_include_directories(libsimple_static PUBLIC ${libsimple_includes})
target_include_directories(libsimple_static_compat PUBLIC ${libsimple_includes})
target_include_directories(libsimple_static_kernel PUBLIC ${libsimple_includes})
target_include_directories(libsimple_static_compat_kernel PUBLIC ${libsimple_includes})

add_to_ramdisk(
	TARGET libsimple
	DESTINATION /sys/lib/libsimple.so
)
