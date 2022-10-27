function(calculate_offsets target_name source outfile)
	cmake_parse_arguments(PARSE_ARGV 3 CALC_OFFSETS "" "JSON;RAW_HEADERS" "HEADERS")

	file(REAL_PATH "${source}" "CALC_OFFSETS_source_real" BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
	file(REAL_PATH "${outfile}" "CALC_OFFSETS_outfile_real" BASE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")

	if (DEFINED CALC_OFFSETS_JSON)
		file(REAL_PATH "${CALC_OFFSETS_JSON}" "CALC_OFFSETS_JSON_real" BASE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
		set(CALC_OFFSETS_JSON_flag "-j")
	else()
		set(CALC_OFFSETS_JSON_real "")
		set(CALC_OFFSETS_JSON_flag "")
	endif()

	if (NOT DEFINED CALC_OFFSETS_RAW_HEADERS)
		foreach(HEADER IN LISTS CALC_OFFSETS_HEADERS)
			list(APPEND CALC_OFFSETS_RAW_HEADERS "-I${HEADER}")
		endforeach()
	endif()

	add_custom_command(
		OUTPUT
			"${CALC_OFFSETS_outfile_real}"
			"${CALC_OFFSETS_JSON_real}"
		COMMAND
			"${CMAKE_SOURCE_DIR}/scripts/calculate-offsets.py"
			"-a"
			"${ANILLO_ARCH}"
			"-s"
			"${CALC_OFFSETS_source_real}"
			"-H"
			"${CALC_OFFSETS_outfile_real}"
			"${CALC_OFFSETS_JSON_flag}"
			"${CALC_OFFSETS_JSON_real}"
			"-d"
			"${CMAKE_CURRENT_BINARY_DIR}/${target_name}.d"
			"--"
			"${CALC_OFFSETS_RAW_HEADERS}"
		DEPFILE
			"${CMAKE_CURRENT_BINARY_DIR}/${target_name}.d"
		DEPENDS
			"${CMAKE_SOURCE_DIR}/scripts/calculate-offsets.py"
			"${CALC_OFFSETS_source_real}"
		VERBATIM
		COMMAND_EXPAND_LISTS
	)

	add_custom_target("${target_name}"
		ALL
		DEPENDS
			"${CALC_OFFSETS_outfile_real}"
	)
endfunction()

function(calculate_offsets_base target_name source outfile base)
	set(CALC_OFFSETS_BASE_DIRS "$<TARGET_PROPERTY:${base},INCLUDE_DIRECTORIES>")
	calculate_offsets("${target_name}" "${source}" "${outfile}" ${ARGN}
		RAW_HEADERS "$<$<BOOL:${CALC_OFFSETS_BASE_DIRS}>:-I$<JOIN:${CALC_OFFSETS_BASE_DIRS},;-I>>"
	)
endfunction()

