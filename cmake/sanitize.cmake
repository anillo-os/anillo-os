function(sanitize_targets)
	if (USERSPACE_ASAN)
		foreach(TARGET IN LISTS ARGN)
			target_link_options("${TARGET}" PRIVATE
				-fsanitize=address
			)
		endforeach()
	endif()
endfunction()
