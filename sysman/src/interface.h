#ifndef _SYSMAN_TEST_INTERFACE_H_
#define _SYSMAN_TEST_INTERFACE_H_

#include <libspooky/libspooky.h>

LIBSYS_STRUCT(sysman_test_interface) {
	spooky_function_t* create_foo_function;
	spooky_function_t* foo_add_function;
	spooky_function_t* foo_count_function;
};

static sysman_test_interface_t sysman_test_interface = {0};
static sys_once_t sysman_test_interface_once = SYS_ONCE_INITIALIZER;

static void sysman_test_interface_init(void* context) {
	spooky_function_parameter_t create_foo_params[] = {
		{
			.type = spooky_type_proxy(),
			.direction = spooky_function_parameter_direction_out,
		},
	};

	sys_abort_status_log(spooky_function_create(true, create_foo_params, sizeof(create_foo_params) / sizeof(*create_foo_params), &sysman_test_interface.create_foo_function));

	spooky_function_parameter_t add_params[] = {
		{
			.type = spooky_type_u64(),
			.direction = spooky_function_parameter_direction_in,
		},
	};

	sys_abort_status_log(spooky_function_create(true, add_params, sizeof(add_params) / sizeof(*add_params), &sysman_test_interface.foo_add_function));

	spooky_function_parameter_t count_params[] = {
		{
			.type = spooky_type_u64(),
			.direction = spooky_function_parameter_direction_out,
		},
	};

	sys_abort_status_log(spooky_function_create(true, count_params, sizeof(count_params) / sizeof(*count_params), &sysman_test_interface.foo_count_function));
};

static void sysman_test_interface_ensure(void) {
	sys_once(&sysman_test_interface_once, sysman_test_interface_init, NULL, 0);
};

#endif // _SYSMAN_TEST_INTERFACE_H_
