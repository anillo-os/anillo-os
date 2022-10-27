project(netman_dev C)

add_subdirectory(e1000)

add_library(netman_dev INTERFACE)

target_link_libraries(netman_dev INTERFACE
	netman_dev_e1000
)
