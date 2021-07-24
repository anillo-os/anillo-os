#include <ferro/core/aarch64/paging.h>

uintptr_t fpage_virtual_to_physical(uintptr_t virtual_address) {
	// the method used in the early version of this function on AARCH64 works always
	return fpage_virtual_to_physical_early(virtual_address);
};
