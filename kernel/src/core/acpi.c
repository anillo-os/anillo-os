#include <stddef.h>

#include <ferro/core/acpi.h>
#include <ferro/core/panic.h>
#include <ferro/core/paging.h>
#include <ferro/core/console.h>
#include <ferro/core/mempool.h>
#include <ferro/core/locks.h>
#include <libk/libk.h>

static facpi_rsdp_t* rsdp = NULL;
static facpi_rsdt_t* rsdt = NULL;
static facpi_xsdt_t* xsdt = NULL;

static facpi_sdt_header_t** tables = NULL;
static size_t table_count = 0;
static flock_spin_intsafe_t tables_lock = FLOCK_SPIN_INTSAFE_INIT;

static bool verify_checksum(const void* data, size_t length) {
	const uint8_t* ptr = data;
	uint8_t checksum = 0;

	while (length-- > 0) {
		checksum += *(ptr++);
	}

	return checksum == 0;
};

facpi_sdt_header_t* facpi_find_table(const char* name) {
	flock_spin_intsafe_lock(&tables_lock);
	for (size_t i = 0; i < table_count; ++i) {
		facpi_sdt_header_t* header = tables[i];

		if (!header) {
			continue;
		}

		if (strncmp(header->signature, name, sizeof(header->signature) / sizeof(*header->signature)) == 0) {
			flock_spin_intsafe_unlock(&tables_lock);
			return header;
		}
	}

	flock_spin_intsafe_unlock(&tables_lock);

	return NULL;
};

ferr_t facpi_register_table(facpi_sdt_header_t* table) {
	flock_spin_intsafe_lock(&tables_lock);

	size_t new_count = table_count + 1;

	if (fmempool_reallocate(tables, new_count * sizeof(*tables), NULL, (void**)&tables) != ferr_ok) {
		return ferr_temporary_outage;
	}

	table_count = new_count;

	tables[table_count - 1] = table;

	flock_spin_intsafe_unlock(&tables_lock);

	return ferr_ok;
};

void facpi_init(facpi_rsdp_t* physical_rsdp) {
	if (!physical_rsdp) {
		fpanic("no RSDP found");
	}

	if (fpage_map_kernel_any(physical_rsdp, fpage_round_up_page(sizeof(*rsdp)) / FPAGE_PAGE_SIZE, (void**)&rsdp, 0) != ferr_ok) {
		fpanic("failed to map RSDP");
	}

	// now verify the RSDP

	// 1. verify the signature
	if (strncmp(rsdp->legacy.signature, "RSD PTR ", 8) != 0) {
		fpanic("invalid RSDP (invalid signature)");
	}

	// 2. verify the checksum for the legacy portion
	if (!verify_checksum(&rsdp->legacy, sizeof(facpi_rsdp_legacy_t))) {
		fpanic("invalid RSDP (invalid checksum for legacy portion)");
	}

	// okay, now, if we're dealing with a modern RSDP, we need to verify the modern portion as well
	if (rsdp->legacy.revision == 2) {
		fconsole_log("found modern RSDP (with XSDT)\n");

		// verify the checksum for the entire table
		if (!verify_checksum(rsdp, rsdp->length)) {
			fpanic("invalid RSDP (invalid checksum for entire table)");
		}

		if (fpage_map_kernel_any((void*)(uintptr_t)rsdp->xsdt_address, fpage_round_up_page(sizeof(*xsdt)) / FPAGE_PAGE_SIZE, (void**)&xsdt, 0) != ferr_ok) {
			fpanic("failed to map XSDT");
		}
	} else {
		fconsole_log("found legacy RSDP (with RSDT)\n");

		if (fpage_map_kernel_any((void*)(uintptr_t)rsdp->legacy.rsdt_address, fpage_round_up_page(sizeof(*rsdt)) / FPAGE_PAGE_SIZE, (void**)&rsdt, 0) != ferr_ok) {
			fpanic("failed to map RSDT");
		}
	}

	if (xsdt) {
		if (!verify_checksum(xsdt, xsdt->header.length)) {
			fpanic("invalid XSDT (invalid checksum)");
		}
	} else {
		if (!verify_checksum(rsdt, rsdt->header.length)) {
			fpanic("invalid RSDT (invalid checksum)");
		}
	}

	table_count = ((xsdt) ? ((xsdt->header.length - sizeof(xsdt->header)) / sizeof(*xsdt->table_pointers)) : ((rsdt->header.length - sizeof(rsdt->header)) / sizeof(*rsdt->table_pointers))) + 1;

	if (fmempool_allocate(table_count * sizeof(*tables), NULL, (void**)&tables) != ferr_ok) {
		fpanic("failed to allocate memory for table pointer array");
	}

	tables[0] = (xsdt) ? (&xsdt->header) : (&rsdt->header);

	for (size_t i = 1; i < table_count; ++i) {
		facpi_sdt_header_t* phys_header = (void*)((xsdt) ? ((uintptr_t)xsdt->table_pointers[i - 1]) : ((uintptr_t)rsdt->table_pointers[i - 1]));
		facpi_sdt_header_t* header = phys_header;
		char tmp[5] = {0};

		if (fpage_map_kernel_any(phys_header, fpage_round_up_to_page_count(sizeof(*header)), (void**)&header, 0) != ferr_ok) {
			fconsole_logf("warning: failed to map ACPI table header at %p\n", header);
			tables[i] = NULL;
			continue;
		}

		if (header->length > fpage_round_up_page(sizeof(*header))) {
			// the table needs more space
			size_t length = header->length;

			if (fpage_unmap_kernel(header, fpage_round_up_to_page_count(sizeof(*header))) != ferr_ok) {
				fpanic("failed to unmap ACPI table header with virtual address %p (this is impossible)\n", header);
			}

			if (fpage_map_kernel_any(phys_header, fpage_round_up_to_page_count(length), (void**)&header, 0) != ferr_ok) {
				fconsole_logf("warning: failed to map ACPI table at %p\n", header);
				tables[i] = NULL;
				continue;
			}
		}

		memcpy(tmp, header->signature, 4);

		fconsole_logf("info: found ACPI table at %p (mapped to %p) with signature \"%s\"\n", phys_header, header, tmp);

		tables[i] = header;
	}
};
