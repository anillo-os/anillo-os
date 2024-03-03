function(encode_file input_path output_path array_name target_name)
	cmake_path(ABSOLUTE_PATH input_path BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" NORMALIZE)
	cmake_path(ABSOLUTE_PATH output_path BASE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}" NORMALIZE)

	add_custom_command(
		OUTPUT
			"${output_path}"
		COMMAND
			"${CMAKE_SOURCE_DIR}/scripts/encode-file.py" "${input_path}" "${output_path}" "${array_name}"
		MAIN_DEPENDENCY
			"${input_path}"
		DEPENDS
			"${CMAKE_SOURCE_DIR}/scripts/encode-file.py"
	)

	add_custom_target("${target_name}" DEPENDS "${output_path}")
endfunction()
