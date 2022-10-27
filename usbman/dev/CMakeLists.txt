project(usbman_dev C)

add_subdirectory(xhci)

add_library(usbman_dev INTERFACE)

target_link_libraries(usbman_dev INTERFACE
	usbman_dev_xhci
)
