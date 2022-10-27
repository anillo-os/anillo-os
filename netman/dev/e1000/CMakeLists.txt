project(netman_dev_e1000 C)

add_subdirectory(libe1000)

add_library(netman_dev_e1000 STATIC
	intel-e1000e.c
)

target_link_libraries(netman_dev_e1000 PRIVATE
	libe1000
	libsys
	libpci
)
