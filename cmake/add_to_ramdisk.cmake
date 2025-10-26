function(add_to_ramdisk)
	cmake_parse_arguments(ADD_TO_RAMDISK "" "TARGET;SOURCE;DESTINATION" "" ${ARGN})

	if (NOT ANILLO_HOST_TESTING)
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
		elseif(DEFINED ADD_TO_RAMDISK_SOURCE)
			get_filename_component(DEST_DIRNAME "${CMAKE_BINARY_DIR}/ramdisksrc/${ADD_TO_RAMDISK_DESTINATION}" DIRECTORY)
			cmake_path(ABSOLUTE_PATH ADD_TO_RAMDISK_SOURCE BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" NORMALIZE)

			string(MAKE_C_IDENTIFIER "${ADD_TO_RAMDISK_SOURCE}" ADD_TO_RAMDISK_TARGET)

			add_custom_command(
				OUTPUT "${CMAKE_BINARY_DIR}/ramdisksrc/${ADD_TO_RAMDISK_DESTINATION}"
				DEPENDS "${ADD_TO_RAMDISK_SOURCE}"
				COMMAND "${CMAKE_COMMAND}" -E make_directory "${DEST_DIRNAME}"
				COMMAND "${CMAKE_COMMAND}" -E copy "${ADD_TO_RAMDISK_SOURCE}" "${CMAKE_BINARY_DIR}/ramdisksrc/${ADD_TO_RAMDISK_DESTINATION}"
			)

			add_custom_target("copy_${ADD_TO_RAMDISK_TARGET}_to_ramdisksrc"
				DEPENDS "${CMAKE_BINARY_DIR}/ramdisksrc/${ADD_TO_RAMDISK_DESTINATION}"
			)

			add_dependencies(generate_ramdisk "copy_${ADD_TO_RAMDISK_TARGET}_to_ramdisksrc")
		else()
			message(FATAL_ERROR "Invalid use of add_to_ramdisk")
		endif()
	endif()
endfunction()
