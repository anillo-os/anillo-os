function(dsym target)
	if (DSYMUTIL_PATH)
		#add_custom_command(
		#	TARGET "${target}"
		#	POST_BUILD
		#	#OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${target}.dSYM"
		#	DEPENDS "${target}"
		#	COMMAND
		#		${CMAKE_COMMAND} -E env "${DSYMUTIL_PATH}" "-o" "${target}.dSYM" "$<TARGET_FILE:${target}>"
		#)
	endif()
endfunction()

function(find_dsymutil)
	find_program(DSYMUTIL_PATH NAMES "llvm-dsymutil" "dsymutil")
endfunction()
