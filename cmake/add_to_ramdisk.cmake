function(add_to_ramdisk)
	cmake_parse_arguments(ADD_TO_RAMDISK "" "TARGET;DESTINATION" "" ${ARGN})

	if (DEFINED ADD_TO_RAMDISK_TARGET)
		get_filename_component(DEST_DIRNAME "${CMAKE_BINARY_DIR}/ramdisksrc/${ADD_TO_RAMDISK_DESTINATION}" DIRECTORY)

		add_custom_command(
			OUTPUT "${CMAKE_BINARY_DIR}/ramdisksrc/${ADD_TO_RAMDISK_DESTINATION}"
			DEPENDS "$<TARGET_FILE:${ADD_TO_RAMDISK_TARGET}>"
			COMMAND "${CMAKE_COMMAND}" -E make_directory "${DEST_DIRNAME}"
			COMMAND "${CMAKE_COMMAND}" -E copy "$<TARGET_FILE:${ADD_TO_RAMDISK_TARGET}>" "${CMAKE_BINARY_DIR}/ramdisksrc/${ADD_TO_RAMDISK_DESTINATION}"
		)

		add_custom_target(copy_${ADD_TO_RAMDISK_TARGET}_to_ramdisksrc
			DEPENDS "${CMAKE_BINARY_DIR}/ramdisksrc/${ADD_TO_RAMDISK_DESTINATION}"
		)

		add_dependencies(generate_ramdisk "copy_${ADD_TO_RAMDISK_TARGET}_to_ramdisksrc")
	else()
		message(FATAL_ERROR "Invalid use of add_to_ramdisk")
	endif()
endfunction()
