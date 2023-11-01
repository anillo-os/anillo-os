project(ferro-core-aarch64 C ASM)

add_library(ferro_core_aarch64 STATIC
	interrupts.c
	per-cpu.c
	generic-timer.c
	gic.c
	scheduler.c
	threads.c
	acpi.c
	serial.c

	ivt.S
	thread-runner.S
	scheduler-helpers.S

	../generic/locks.c
)

add_dependencies(ferro_core_aarch64
	generate_offsets_header
)

target_link_libraries(ferro_core_aarch64 PRIVATE
	libsimple_static_kernel
)
