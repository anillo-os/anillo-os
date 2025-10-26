function(add_anillo_dylib target_name)
	cmake_parse_arguments(ANILLO_DYLIB "RAMDISK" "NAME;VERSION;COMPAT_VERSION;INSTALL_PATH" "SOURCES" ${ARGN})

	if (NOT DEFINED ANILLO_DYLIB_NAME)
		string(REGEX REPLACE "^lib" "" ANILLO_DYLIB_NAME "${target_name}")
	endif()

	if (NOT DEFINED ANILLO_DYLIB_VERSION)
		set(ANILLO_DYLIB_VERSION 1)
	endif()

	if (NOT DEFINED ANILLO_DYLIB_COMPAT_VERSION)
		set(ANILLO_DYLIB_COMPAT_VERSION ${ANILLO_DYLIB_VERSION})
	endif()

	if (NOT DEFINED ANILLO_DYLIB_INSTALL_PATH)
		message(FATAL_ERROR "Dynamic library install path required")
	endif()

	if (NOT DEFINED ANILLO_DYLIB_SOURCES)
		set(ANILLO_DYLIB_SOURCES "${CMAKE_SOURCE_DIR}/cmake/dummy.c")
	endif()

	anillo_exit_asm_hack()

	add_library(${target_name} SHARED ${ANILLO_DYLIB_SOURCES} "${CMAKE_BINARY_DIR}/exit-asm.o")

	set_target_properties(${target_name} PROPERTIES
		PREFIX "lib"
		SUFFIX ".dylib"
		OUTPUT_NAME "${ANILLO_DYLIB_NAME}"
	)

	if (ANILLO_HOST_TESTING)
		target_compile_options(${target_name} PRIVATE
			-nostdlib
		)
		target_link_options(${target_name} PRIVATE
			-nostdlib
		)
	else()
		target_link_options(${target_name} PRIVATE
			-Wl,-dylib
			-Wl,-dylib_install_name,${ANILLO_DYLIB_INSTALL_PATH}
			-Wl,-dylib_current_version,${ANILLO_DYLIB_VERSION}
			-Wl,-dylib_compatibility_version,${ANILLO_DYLIB_COMPAT_VERSION}
		)
	endif()

	if (ANILLO_DYLIB_RAMDISK)
		add_to_ramdisk(
			TARGET ${target_name}
			DESTINATION "${ANILLO_DYLIB_INSTALL_PATH}"
		)
	endif()
endfunction()
