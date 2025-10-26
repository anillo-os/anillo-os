project(netman C)

include_directories(
	include
)

add_subdirectory(dev)

add_link_options(
	-fvisibility=hidden
)

add_anillo_executable(netman
	RAMDISK
	INSTALL_PATH /sys/netman/netman
	SOURCES
		src/netman.c
		src/device.c
		src/packet.c
		src/ether.c
		src/arp.c
		src/ip.c
		src/objects.c
		src/icmp.c
		src/udp.c
		src/tcp.c

		src/test.c
)

add_to_ramdisk(
	SOURCE resources/org.anillo.netman.json5
	DESTINATION /sys/config/sysman/org.anillo.netman.json5
)

target_include_directories(netman PRIVATE
	"${CMAKE_CURRENT_BINARY_DIR}"
)

target_link_libraries(netman PUBLIC
	libc
	libsys
	libsimple
	libeve
	libspooky
	libpci
)

target_link_libraries(netman PRIVATE
	netman_dev
)

sanitize_targets(
	netman
)
