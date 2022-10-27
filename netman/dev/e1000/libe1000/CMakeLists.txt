project(libe1000 C)

add_library(libe1000 STATIC
	e1000_80003es2lan.c
	e1000_82540.c
	e1000_82541.c
	e1000_82542.c
	e1000_82543.c
	e1000_82571.c
	e1000_api.c
	e1000_ich8lan.c
	e1000_mac.c
	e1000_manage.c
	e1000_nvm.c
	e1000_osdep.c
	e1000_phy.c
)

set_target_properties(libe1000 PROPERTIES
	OUTPUT_NAME e1000
)

target_link_libraries(libe1000 PRIVATE
	libsimple
	libsys
	libpci
)

target_include_directories(libe1000 PUBLIC
	include
)
