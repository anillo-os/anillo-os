project(ferro-core-x86_64 C ASM)

add_library(ferro_core_x86_64 STATIC
	paging.c
	interrupts.c
	per-cpu.c
	apic.c
	tsc.c
	scheduler.c
	threads.c
	serial.c
	smp-init.c

	interrupt-wrappers.S
	scheduler-helpers.S
	thread-runner.S
	smp-init.S

	../generic/locks.c
)

add_dependencies(ferro_core_x86_64
	generate_offsets_header
)

target_link_libraries(ferro_core_x86_64 PRIVATE
	libsimple_static_kernel
)
