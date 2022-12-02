function(spooky_gen target_name rpc_def_path)
	cmake_parse_arguments(SPOOKY_GEN "SERVER" "BASENAME;SOURCE;HEADER" "" ${ARGN})

	file(REAL_PATH "${rpc_def_path}" SPOOKY_GEN_RPC_DEF_PATH BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

	if (SPOOKY_GEN_SERVER)
		set(SPOOKY_GEN_SUFFIX "server")
		set(SPOOKY_GEN_SERVER_ARG "--server")
	else()
		set(SPOOKY_GEN_SUFFIX "client")
		set(SPOOKY_GEN_SERVER_ARG "")
	endif()

	if (NOT DEFINED SPOOKY_GEN_BASENAME)
		get_filename_component(SPOOKY_GEN_BASENAME "${rpc_def_path}" NAME_WE)
	endif()

	if (NOT DEFINED SPOOKY_GEN_SOURCE)
		set(SPOOKY_GEN_SOURCE "${CMAKE_CURRENT_BINARY_DIR}/${SPOOKY_GEN_BASENAME}.${SPOOKY_GEN_SUFFIX}.c")
	endif()

	if (NOT DEFINED SPOOKY_GEN_HEADER)
		set(SPOOKY_GEN_HEADER "${CMAKE_CURRENT_BINARY_DIR}/${SPOOKY_GEN_BASENAME}.${SPOOKY_GEN_SUFFIX}.h")
	endif()

	add_custom_command(
		OUTPUT
			"${SPOOKY_GEN_SOURCE}"
			"${SPOOKY_GEN_HEADER}"
		COMMAND
			"${CMAKE_SOURCE_DIR}/libspooky/scripts/spookygen.py"
			"-i"
			"${SPOOKY_GEN_RPC_DEF_PATH}"
			"-s"
			"${SPOOKY_GEN_SOURCE}"
			"-H"
			"${SPOOKY_GEN_HEADER}"
			"${SPOOKY_GEN_SERVER_ARG}"
		DEPENDS
			"${CMAKE_SOURCE_DIR}/libspooky/scripts/spookygen.py"
			"${SPOOKY_GEN_RPC_DEF_PATH}"
		WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
	)

	add_custom_target("${target_name}"
		ALL
		DEPENDS
			"${SPOOKY_GEN_SOURCE}"
			"${SPOOKY_GEN_HEADER}"
	)
endfunction()
