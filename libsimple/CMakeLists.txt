project(libsimple C)

set(libsimple_sources
	src/libsimple.c
	src/ghmap.c
	src/ring.c
	#src/vector.c
	src/mempool.c
	src/unicode.c
)

set(libsimple_implementations_generic
	memcpy
	memmove
	memset
)

set(libsimple_implementations_x86_64
	memcpy
	memmove
	memset
)

set(libsimple_force_optimize
	memcpy
	memmove
	memset
)

set(libsimple_force_optimization_sources "")

foreach(arch_impl IN LISTS libsimple_implementations_${ANILLO_ARCH})
	list(APPEND libsimple_sources src/${ANILLO_ARCH}/${arch_impl}.c)

	if ("${arch_impl}" IN_LIST libsimple_force_optimize)
		list(APPEND libsimple_force_optimization_sources src/${ANILLO_ARCH}/${arch_impl}.c)
	endif()
endforeach()

foreach(generic_impl IN LISTS libsimple_implementations_generic)
	if ("${generic_impl}" IN_LIST libsimple_implementations_${ANILLO_ARCH})
		continue()
	endif()

	list(APPEND libsimple_sources src/generic/${generic_impl}.c)
	
	if ("${generic_impl}" IN_LIST libsimple_force_optimize)
		list(APPEND libsimple_force_optimization_sources src/generic/${generic_impl}.c)
	endif()
endforeach()

set(libsimple_includes
	include
)

if (NOT ANILLO_HOST_TESTING)
	add_anillo_dylib(libsimple
		RAMDISK
		INSTALL_PATH /sys/lib/libsimple.dylib
		SOURCES
			${libsimple_sources}
	)
endif()

add_library(libsimple_static STATIC ${libsimple_sources})
add_library(libsimple_static_kernel STATIC ${libsimple_sources})
add_library(libsimple_static_compat INTERFACE)
add_library(libsimple_static_kernel_compat INTERFACE)

# setting the property using set_source_files_properties("${libsimple_force_optimization_sources}" ...) doesn't work properly
foreach(forced_opt IN LISTS libsimple_force_optimization_sources)
	set_source_files_properties("${forced_opt}" PROPERTIES
		COMPILE_OPTIONS "-O3"
	)
endforeach()

if (ANILLO_HOST_TESTING)
	target_link_options(libsimple_static_compat INTERFACE
		-Wl,--defsym,memset=simple_memset
		-Wl,--defsym,memcpy=simple_memcpy
		-Wl,--defsym,bzero=simple_bzero
		-Wl,--defsym,__bzero=simple_bzero
	)
else()
	target_link_options(libsimple_static_compat INTERFACE
		-Wl,-alias,_simple_memset,_memset
		-Wl,-alias,_simple_memcpy,_memcpy
		-Wl,-alias,_simple_bzero,_bzero
		-Wl,-alias,_simple_bzero,___bzero
	)
endif()

target_link_options(libsimple_static_kernel_compat INTERFACE
	-Wl,--defsym,memset=simple_memset
	-Wl,--defsym,memcpy=simple_memcpy
	-Wl,--defsym,bzero=simple_bzero
	-Wl,--defsym,__bzero=simple_bzero
)

if (NOT ANILLO_HOST_TESTING)
	target_link_libraries(libsimple PRIVATE
		libsimple_static_compat
		libdymple
	)
endif()

set(libsimple_kernel_compile_options
	-fno-plt
	-fno-pic
	-mcmodel=large

	-fno-stack-protector
	-fno-stack-check
	-mno-red-zone

	-target "${ANILLO_TARGET_TRIPLE_KERNEL}"
)

if (FERRO_UBSAN AND FERRO_KASAN)
	set(libsimple_kernel_compile_options
		${libsimple_kernel_compile_options}

		-fsanitize=undefined,kernel-address
	)
elseif(FERRO_UBSAN)
	set(libsimple_kernel_compile_options
		${libsimple_kernel_compile_options}

		-fsanitize=undefined
	)
elseif(FERRO_KASAN)
	set(libsimple_kernel_compile_options
		${libsimple_kernel_compile_options}

		-fsanitize=kernel-address
	)
endif()

if(FERRO_KASAN)
	# see `kernel/src/CMakeLists.txt`
	set(libsimple_kernel_compile_options
		${libsimple_kernel_compile_options}

		"SHELL:-mllvm -asan-mapping-offset=0xe0000e8000000000"
		"SHELL:-mllvm -asan-globals=false"

		-DFERRO_KASAN=1
	)
endif()

set(libsimple_general_compile_options
	-fno-omit-frame-pointer
)

target_compile_options(libsimple_static_kernel PRIVATE ${libsimple_kernel_compile_options} ${libsimple_general_compile_options})
target_compile_options(libsimple_static PRIVATE ${libsimple_general_compile_options})

set_target_properties(libsimple_static PROPERTIES
	OUTPUT_NAME simple
)

set_target_properties(libsimple_static_kernel PROPERTIES
	OUTPUT_NAME simple_kernel
)

if (NOT ANILLO_HOST_TESTING)
	target_include_directories(libsimple PUBLIC ${libsimple_includes})
endif()
target_include_directories(libsimple_static PUBLIC ${libsimple_includes})
target_include_directories(libsimple_static_kernel PUBLIC ${libsimple_includes})

if (NOT ANILLO_HOST_TESTING)
	target_link_libraries(libsimple PUBLIC ferro_headers_base)
endif()
target_link_libraries(libsimple_static PUBLIC ferro_headers_base)
target_link_libraries(libsimple_static_kernel PUBLIC ferro_headers_base)

add_library(libsimple_headers INTERFACE)
target_include_directories(libsimple_headers INTERFACE ${libsimple_includes})
target_link_libraries(libsimple_headers INTERFACE ferro_headers_base)

if (NOT ANILLO_HOST_TESTING)
	sanitize_targets(libsimple)
endif()

sanitize_targets(libsimple_static)
