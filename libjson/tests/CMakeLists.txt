project(libjson-tests C)

encode_file(
	test.json
	test.json.h
	test_json_data
	generate_test_json_data
)

encode_file(
	test.json5
	test.json5.h
	test_json5_data
	generate_test_json5_data
)

if (ANILLO_HOST_TESTING)
	add_executable(libjson-tests test.c)

	target_link_libraries(libjson-tests PRIVATE
		libjson_static
		libsys_host
		libmath_static
		libsimple_static
	)
else()
	add_anillo_executable(libjson-tests
		RAMDISK
		INSTALL_PATH /sys/tests/libjson-tests
		SOURCES
			test.c
	)

	target_link_libraries(libjson-tests PRIVATE
		libc
		libsys
		libjson
		libsimple
	)
endif()

target_include_directories(libjson-tests PRIVATE
	"${CMAKE_CURRENT_BINARY_DIR}"
)

add_dependencies(libjson-tests
	generate_test_json_data
	generate_test_json5_data
)
