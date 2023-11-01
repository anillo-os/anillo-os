project(ferro-mm C)

add_library(ferro_mm STATIC
	mempool.c
	paging.c
	pmm.c
	vmm.c
	slab.c
)

add_subdirectory("${ANILLO_ARCH}")

target_link_libraries(ferro_mm PUBLIC
	ferro_mm_${ANILLO_ARCH}
)

target_link_libraries(ferro_mm PRIVATE
	libsimple_static_kernel
)
