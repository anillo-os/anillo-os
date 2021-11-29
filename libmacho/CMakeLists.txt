project(libmacho C)

add_library(libmacho INTERFACE)

# dummy library used for compile_commands.json to work properly
add_library(libmacho_dummy STATIC
	src/main.c
)

target_include_directories(libmacho INTERFACE include)
target_link_libraries(libmacho INTERFACE ferro_headers_base)

target_link_libraries(libmacho_dummy PUBLIC libmacho)
