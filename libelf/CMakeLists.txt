project(libelf C)

add_library(libelf INTERFACE)

# dummy library used for compile_commands.json to work properly
add_library(libelf_dummy STATIC
	src/main.c
)

target_include_directories(libelf INTERFACE include)
target_link_libraries(libelf INTERFACE ferro_headers_base)

target_link_libraries(libelf_dummy PUBLIC libelf)
