project(usbman_dev_xhci C)

add_library(usbman_dev_xhci STATIC
	xhci.c
)

target_link_libraries(usbman_dev_xhci PRIVATE
	libsys
	libpci
)
