project(usbman C)

include_directories(
	include
)

add_subdirectory(dev)

add_link_options(
	-fvisibility=hidden
)

add_anillo_executable(usbman
	RAMDISK
	INSTALL_PATH /sys/usbman/usbman
	SOURCES
		src/usbman.c
		src/usb.c
		src/hid.c
		src/objects.c
)

add_to_ramdisk(
	SOURCE resources/org.anillo.usbman.json5
	DESTINATION /sys/config/sysman/org.anillo.usbman.json5
)

target_include_directories(usbman PRIVATE
	"${CMAKE_CURRENT_BINARY_DIR}"
)

target_link_libraries(usbman PUBLIC
	libc
	libsys
	libsimple
	libeve
	libspooky
	libpci
)

target_link_libraries(usbman PRIVATE
	usbman_dev
)

sanitize_targets(
	usbman
)
