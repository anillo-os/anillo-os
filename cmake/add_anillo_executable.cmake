function(add_anillo_executable target_name)
	cmake_parse_arguments(ANILLO_EXEC "RAMDISK" "NAME;INSTALL_PATH" "SOURCES" ${ARGN})

	if (NOT DEFINED ANILLO_EXEC_NAME)
		set(ANILLO_EXEC_NAME "${target_name}")
	endif()

	if (NOT DEFINED ANILLO_EXEC_SOURCES)
		set(ANILLO_EXEC_SOURCES "${CMAKE_SOURCE_DIR}/cmake/dummy.c")
	endif()

	add_executable(${target_name} ${ANILLO_EXEC_SOURCES})

	set_target_properties(${target_name} PROPERTIES
		PREFIX ""
		SUFFIX ""
		OUTPUT_NAME "${ANILLO_EXEC_NAME}"
	)

	target_compile_options(${target_name} PRIVATE
		-fPIC
	)

	target_link_options(${target_name} PRIVATE
		-Wl,-pie
		-Wl,-execute
	)

	if (ANILLO_EXEC_RAMDISK)
		if (NOT DEFINED ANILLO_EXEC_INSTALL_PATH)
			message(FATAL_ERROR "Executable install path required for installing to ramdisk")
		endif()

		add_to_ramdisk(
			TARGET ${target_name}
			DESTINATION "${ANILLO_EXEC_INSTALL_PATH}"
		)
	endif()
endfunction()
