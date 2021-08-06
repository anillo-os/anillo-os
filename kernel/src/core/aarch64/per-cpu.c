#include <ferro/core/aarch64/per-cpu.h>

// for now, we only ever operate on a single CPU
// however, once we enable SMP, we can extend this

static farch_per_cpu_data_t data = {
	.base = &data,
};

farch_per_cpu_data_t* farch_per_cpu_base_address(void) {
	return &data;
};
