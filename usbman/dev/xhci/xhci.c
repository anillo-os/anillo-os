/*
 * This file is part of Anillo OS
 * Copyright (C) 2022 Anillo OS Developers
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <usbman/dev/xhci/xhci.private.h>

#include <libpci/libpci.h>
#include <libsimple/libsimple.h>
#include <ferro/bits.h>

#define PAGE_ALIGNMENT 12

#if FERRO_ARCH == FERRO_ARCH_x86_64
	#include <immintrin.h>

	#define usbman_xhci_memory_barrier _mm_mfence
#else
	#define usbman_xhci_memory_barrier() __atomic_thread_fence(__ATOMIC_SEQ_CST)
#endif

// FOR DEBUGGING
#define XHCI_WATCHDOG 1

static void usbman_xhci_sleep_us(uint64_t us) {
	if (us == 0) {
		return;
	}
	sys_abort_status_log(sys_thread_suspend_timeout(sys_thread_current(), us * 1000, sys_timeout_type_relative_ns_monotonic));
};

static USBMAN_WUR ferr_t usbman_xhci_ring_common_init(usbman_xhci_ring_common_t* ring, size_t entry_count) {
	ferr_t status = ferr_ok;
	size_t size_in_bytes = 0;
	size_t page_count = 0;

	simple_memset(ring, 0, sizeof(*ring));

	sys_mutex_init(&ring->mutex);

	ring->entry_count = entry_count;
	size_in_bytes = ring->entry_count * sizeof(usbman_xhci_trb_t);
	page_count = sys_page_round_up_count(size_in_bytes);

	status = sys_page_allocate(page_count, sys_page_flag_contiguous | sys_page_flag_prebound | sys_page_flag_uncacheable, (void*)&ring->entries);
	if (status != ferr_ok) {
		goto out;
	}

	status = sys_page_translate((void*)ring->entries, (void*)&ring->physical_start);
	if (status != ferr_ok) {
		goto out;
	}

	simple_memset((void*)ring->entries, 0, size_in_bytes);

	// initially, the cycle bit must be `1` for a TRB to be owned by the consumer
	// (same logic applies to both producer and consumer rings)
	ring->cycle_state = true;

	ring->dequeue = ring->entries;

out:
	if (status != ferr_ok) {
		if (ring->entries) {
			USBMAN_WUR_IGNORE(sys_page_free((void*)ring->entries));
		}
	}

	return status;
};

static void usbman_xhci_ring_common_destroy(usbman_xhci_ring_common_t* ring) {
	size_t size_in_bytes = ring->entry_count * sizeof(usbman_xhci_trb_t);
	size_t page_count = sys_page_round_up_count(size_in_bytes);

	if (ring->entries) {
		USBMAN_WUR_IGNORE(sys_page_free((void*)ring->entries));
	}
};

ferr_t usbman_xhci_producer_ring_init(usbman_xhci_producer_ring_t* ring) {
	ferr_t status = ferr_ok;
	volatile usbman_xhci_trb_t* link_trb = NULL;

	simple_memset(ring, 0, sizeof(*ring));

	status = sys_mempool_allocate(sizeof(*ring->callbacks) * FUSB_XHCI_PRODUCER_RING_DEFAULT_ENTRY_COUNT, NULL, (void*)&ring->callbacks);
	if (status != ferr_ok) {
		goto out;
	}

	simple_memset(ring->callbacks, 0, sizeof(*ring->callbacks) * FUSB_XHCI_PRODUCER_RING_DEFAULT_ENTRY_COUNT);

	status = usbman_xhci_ring_common_init(&ring->common, FUSB_XHCI_PRODUCER_RING_DEFAULT_ENTRY_COUNT + 1);
	if (status != ferr_ok) {
		goto out;
	}

	ring->enqueue = ring->common.entries;

	// we always have one less entry because of the link TRB
	--ring->common.entry_count;

	// initialize the link TRB
	link_trb = &ring->common.entries[ring->common.entry_count];
	link_trb->parameters[0] = (uintptr_t)ring->common.physical_start & 0xffffffff;
	link_trb->parameters[1] = (uintptr_t)ring->common.physical_start >> 32;

	// target interrupter 0
	// doesn't really matter since we don't set interrupt-on-completion
	link_trb->status = 0;

	// type = link, toggle cycle on
	link_trb->control = (1 << 1) | ((uint32_t)usbman_xhci_trb_type_link << 10);

out:
	if (status != ferr_ok) {
		if (ring->callbacks) {
			USBMAN_WUR_IGNORE(sys_mempool_free(ring->callbacks));
		}
	}

	return status;
};

void usbman_xhci_producer_ring_destroy(usbman_xhci_producer_ring_t* ring) {
	USBMAN_WUR_IGNORE(sys_mempool_free(ring->callbacks));
	++ring->common.entry_count; // add back the link TRB
	usbman_xhci_ring_common_destroy(&ring->common);
};

static volatile usbman_xhci_trb_t* usbman_xhci_producer_ring_next_trb_locked(usbman_xhci_producer_ring_t* ring, volatile usbman_xhci_trb_t* trb, volatile usbman_xhci_trb_t** out_link_trb, bool* out_wrapped) {
	volatile usbman_xhci_trb_t* next_trb = trb + 1;
	bool wrapped = false;

	if (usbman_xhci_trb_get_type(next_trb) == usbman_xhci_trb_type_link) {
		// TODO: we only support single segment rings for now.
		//       when we support multi-segment rings, we'll have to change this.

		if (out_link_trb) {
			*out_link_trb = next_trb;
		}

		next_trb = &ring->common.entries[0];
		wrapped = true;
	} else if (out_link_trb) {
		*out_link_trb = NULL;
	}

	if (out_wrapped) {
		*out_wrapped = wrapped;
	}

	return next_trb;
};

static size_t usbman_xhci_producer_ring_trb_index(usbman_xhci_producer_ring_t* ring, volatile usbman_xhci_trb_t* trb) {
	// TODO: change this when adding multi-segment ring support
	return ((uintptr_t)trb - (uintptr_t)ring->common.entries) / sizeof(usbman_xhci_trb_t);
};

ferr_t usbman_xhci_producer_ring_produce(usbman_xhci_producer_ring_t* ring, const usbman_xhci_trb_t* trb, usbman_xhci_producer_ring_callback_f callback, void* context) {
	ferr_t status = ferr_ok;
	volatile usbman_xhci_trb_t* next_trb = NULL;
	volatile usbman_xhci_trb_t* link_trb = NULL;
	bool wrapped = false;
	size_t trb_index = 0;

	eve_mutex_lock(&ring->common.mutex);

	next_trb = usbman_xhci_producer_ring_next_trb_locked(ring, ring->enqueue, &link_trb, &wrapped);

	if (next_trb == ring->common.dequeue) {
		// ring is full
		status = ferr_temporary_outage;
		goto out;
	}

	// instead of doing a memcpy, let's assign the values manually
	// this is because we need to ensure that the control value is written last
	// (TODO: check if out-of-order execution messes this up as well)
	ring->enqueue->parameters[0] = trb->parameters[0];
	usbman_xhci_memory_barrier();
	ring->enqueue->parameters[1] = trb->parameters[1];
	usbman_xhci_memory_barrier();
	ring->enqueue->status = trb->status;

	usbman_xhci_memory_barrier();

	// control is special because we need to adjust to match the current ring state (i.e. the cycle bit)
	ring->enqueue->control = (trb->control & ~1) | (ring->common.cycle_state ? 1 : 0);

	trb_index = usbman_xhci_producer_ring_trb_index(ring, ring->enqueue);
	ring->callbacks[trb_index].callback = callback;
	ring->callbacks[trb_index].context = context;

	ring->enqueue = next_trb;

	if (link_trb) {
		// we need to give the link TRB to the consumer
		link_trb->control = (link_trb->control & ~1) | (ring->common.cycle_state ? 1 : 0);
	}

	if (wrapped) {
		ring->common.cycle_state = !ring->common.cycle_state;
	}

out:
	sys_mutex_unlock(&ring->common.mutex);
out_unlocked:
	return status;
};

ferr_t usbman_xhci_producer_ring_notify_consume(usbman_xhci_producer_ring_t* ring, const usbman_xhci_trb_t* completion_trb, usbman_xhci_trb_t* out_consumed_trb, usbman_xhci_producer_ring_callback_entry_t* out_callback_entry) {
	ferr_t status = ferr_ok;
	volatile usbman_xhci_trb_t* next_trb = NULL;
	size_t trb_index = 0;
	usbman_xhci_trb_t consumed_trb;

	eve_mutex_lock(&ring->common.mutex);

	if (ring->common.dequeue == ring->enqueue) {
		// ring is empty
		status = ferr_temporary_outage;
		goto out;
	}

	next_trb = usbman_xhci_producer_ring_next_trb_locked(ring, ring->common.dequeue, NULL, NULL);

	trb_index = usbman_xhci_producer_ring_trb_index(ring, ring->common.dequeue);
	simple_memcpy(out_callback_entry, &ring->callbacks[trb_index], sizeof(*out_callback_entry));
	simple_memset(&ring->callbacks[trb_index], 0, sizeof(*out_callback_entry));

	simple_memcpy(&consumed_trb, (void*)ring->common.dequeue, sizeof(consumed_trb));

	if (out_consumed_trb) {
		simple_memcpy(out_consumed_trb, &consumed_trb, sizeof(*out_consumed_trb));
	}

	ring->common.dequeue = next_trb;

out:
	sys_mutex_unlock(&ring->common.mutex);
out_unlocked:
	return status;
};

static volatile usbman_xhci_trb_t* usbman_xhci_consumer_ring_next_trb_locked(usbman_xhci_consumer_ring_t* ring, volatile usbman_xhci_trb_t* trb, bool* out_wrapped) {
	volatile usbman_xhci_trb_t* next_trb = trb + 1;
	bool wrapped = false;

	// TODO: change this when we add multi-segment ring support
	if (next_trb >= &ring->common.entries[0] + ring->common.entry_count) {
		next_trb = &ring->common.entries[0];
		wrapped = true;
	}

	if (out_wrapped) {
		*out_wrapped = wrapped;
	}

	return next_trb;
};

ferr_t usbman_xhci_consumer_ring_init(usbman_xhci_consumer_ring_t* ring) {
	ferr_t status = ferr_ok;

	simple_memset(ring, 0, sizeof(*ring));

	status = usbman_xhci_ring_common_init(&ring->common, FUSB_XHCI_CONSUMER_RING_DEFAULT_ENTRY_COUNT);
	if (status != ferr_ok) {
		goto out;
	}

	ring->physical_dequeue = ring->common.physical_start;

out:
	return status;
};

void usbman_xhci_consumer_ring_destroy(usbman_xhci_consumer_ring_t* ring) {
	usbman_xhci_ring_common_destroy(&ring->common);
};

ferr_t usbman_xhci_consumer_ring_consume(usbman_xhci_consumer_ring_t* ring, usbman_xhci_trb_t* out_trb) {
	ferr_t status = ferr_ok;
	volatile usbman_xhci_trb_t* next_trb = NULL;
	bool dequeue_cycle_state = false;
	bool wrapped = false;

	eve_mutex_lock(&ring->common.mutex);

	dequeue_cycle_state = (ring->common.dequeue->control & 1) != 0;

	if (dequeue_cycle_state != ring->common.cycle_state) {
		// ring is empty
		status = ferr_temporary_outage;
		goto out;
	}

	if (out_trb) {
		simple_memcpy(out_trb, (void*)ring->common.dequeue, sizeof(*out_trb));
	}

	next_trb = usbman_xhci_consumer_ring_next_trb_locked(ring, ring->common.dequeue, &wrapped);

	ring->common.dequeue = next_trb;

	// this also needs to change when we add multi-segment ring support
	ring->physical_dequeue = ring->common.physical_start + ((uintptr_t)next_trb - (uintptr_t)ring->common.entries);

	if (wrapped) {
		ring->common.cycle_state = !ring->common.cycle_state;
	}

out:
	sys_mutex_unlock(&ring->common.mutex);
out_unlocked:
	return status;
};

static void usbman_xhci_event_ring_poll_worker(void* context) {
	usbman_xhci_event_ring_t* event_ring = context;
	usbman_xhci_producer_ring_callback_entry_t callback_entry;

	while (true) {
		usbman_xhci_trb_t event;
		usbman_xhci_trb_type_t type;
		usbman_xhci_trb_t consumed_trb;

		if (usbman_xhci_event_ring_consume(event_ring, &event) != ferr_ok) {
			break;
		}

		type = usbman_xhci_trb_get_type(&event);

		//sys_console_log_f("XHCI: got event with type=%u\n", type);

		if (type == usbman_xhci_trb_type_command_completion_event) {
			//sys_console_log("XHCI: event indicates command completion; notifying command ring...\n");

			if (usbman_xhci_command_ring_notify_consume(&event_ring->controller->command_ring, &event, &consumed_trb, &callback_entry) != ferr_ok) {
				sys_console_log("XHCI: failed to notify command ring about command completion\n");
			}

			if (callback_entry.callback) {
				callback_entry.callback(callback_entry.context, &consumed_trb, &event);
			}
		} else if (type == usbman_xhci_trb_type_transfer_event) {
			usbman_xhci_port_t* port = NULL;
			ferr_t status = ferr_ok;
			uint8_t slot_id = event.control >> 24;
			uint8_t port_number;
			uint8_t dci = (event.control >> 16) & 0x1ff;

			//sys_console_log_f("XHCI: event indicates transfer completion; notifying transfer ring #%u on device on slot #%u...\n", dci, slot_id);

			eve_mutex_lock(&event_ring->controller->ports_mutex);

			port_number = event_ring->controller->slots_to_ports[slot_id];
			status = simple_ghmap_lookup_h(&event_ring->controller->ports, port_number, false, 0, NULL, (void*)&port, NULL);

			sys_mutex_unlock(&event_ring->controller->ports_mutex);

			if (status == ferr_ok) {
				if (usbman_xhci_transfer_ring_notify_consume(&port->transfer_rings[dci - 1], &event, &consumed_trb, &callback_entry) != ferr_ok) {
					sys_console_log_f("XHCI: port #%u: failed to notify transfer ring #%u", port->port_number, dci);
				}

				if (callback_entry.callback) {
					callback_entry.callback(callback_entry.context, &consumed_trb, &event);
				}
			} else {
				sys_console_log_f("XHCI: failed to find device on slot #%u\n", slot_id);
			}
		}
	}

	usbman_xhci_event_ring_done_processing(event_ring);
};

ferr_t usbman_xhci_event_ring_init(usbman_xhci_event_ring_t* event_ring, volatile uint64_t* dequeue_pointer, usbman_xhci_controller_t* controller) {
	ferr_t status = ferr_ok;
	size_t table_size_in_bytes = sizeof(usbman_xhci_erst_entry_t) * 1;
	size_t table_page_count = sys_page_round_up_count(table_size_in_bytes);

	simple_memset(event_ring, 0, sizeof(*event_ring));

	event_ring->dequeue_pointer = dequeue_pointer;
	event_ring->controller = controller;

	status = sys_page_allocate(table_page_count, sys_page_flag_contiguous | sys_page_flag_prebound | sys_page_flag_uncacheable, (void*)&event_ring->table);
	if (status != ferr_ok) {
		goto out;
	}

	status = sys_page_translate((void*)event_ring->table, (void*)&event_ring->physical_table);
	if (status != ferr_ok) {
		goto out;
	}

	simple_memset((void*)event_ring->table, 0, table_size_in_bytes);

	status = usbman_xhci_consumer_ring_init(&event_ring->ring);
	if (status != ferr_ok) {
		goto out;
	}

	event_ring->table[0].address_low = (uintptr_t)event_ring->ring.common.physical_start & 0xffffffff;
	event_ring->table[0].address_high = (uintptr_t)event_ring->ring.common.physical_start >> 32;
	event_ring->table[0].segment_size = event_ring->ring.common.entry_count;

out:
	if (status != ferr_ok) {
		// TODO: clean up consumer ring

		if (event_ring->table) {
			USBMAN_WUR_IGNORE(sys_page_free((void*)event_ring->table));
		}
	}

	return status;
};


ferr_t usbman_xhci_event_ring_consume(usbman_xhci_event_ring_t* event_ring, usbman_xhci_trb_t* out_trb) {
	return usbman_xhci_consumer_ring_consume(&event_ring->ring, out_trb);
};

void usbman_xhci_event_ring_done_processing(usbman_xhci_event_ring_t* event_ring) {
	// TODO: change this once multi-segment support is added
	//       (we would have to update the DESI bits properly)
	//
	// write 1 to the "event handler busy" bit to clear it
	*event_ring->dequeue_pointer = (uintptr_t)event_ring->ring.physical_dequeue | (1 << 3);
};

void usbman_xhci_event_ring_schedule_poll(usbman_xhci_event_ring_t* event_ring) {
	//sys_console_log_f("XHCI: received interrupt; scheduling poll worker\n");
	USBMAN_WUR_IGNORE(eve_loop_enqueue(eve_loop_get_main(), usbman_xhci_event_ring_poll_worker, event_ring));
};

ferr_t usbman_xhci_command_ring_init(usbman_xhci_command_ring_t* command_ring, usbman_xhci_controller_t* controller) {
	simple_memset(command_ring, 0, sizeof(*command_ring));

	command_ring->controller = controller;

	return usbman_xhci_producer_ring_init(&command_ring->ring);
};

ferr_t usbman_xhci_command_ring_produce(usbman_xhci_command_ring_t* command_ring, const usbman_xhci_trb_t* trb, usbman_xhci_producer_ring_callback_f callback, void* context) {
	ferr_t status = usbman_xhci_producer_ring_produce(&command_ring->ring, trb, callback, context);

	if (status == ferr_ok) {
		// ring the command ring doorbell
		usbman_xhci_memory_barrier();
		command_ring->controller->doorbell_array[0] = usbman_xhci_doorbell_make(0, 0);
		// flush the write
		volatile uint32_t tmp = command_ring->controller->doorbell_array[0];
	}

	return status;
};

ferr_t usbman_xhci_command_ring_notify_consume(usbman_xhci_command_ring_t* command_ring, const usbman_xhci_trb_t* completion_trb, usbman_xhci_trb_t* out_consumed_trb, usbman_xhci_producer_ring_callback_entry_t* out_callback_entry) {
	return usbman_xhci_producer_ring_notify_consume(&command_ring->ring, completion_trb, out_consumed_trb, out_callback_entry);
};

ferr_t usbman_xhci_transfer_ring_init(usbman_xhci_transfer_ring_t* transfer_ring, usbman_xhci_controller_t* controller, uint8_t slot_id, uint8_t dci) {
	ferr_t status = ferr_ok;

	simple_memset(transfer_ring, 0, sizeof(*transfer_ring));

	transfer_ring->controller = controller;
	transfer_ring->slot_id = slot_id;
	transfer_ring->dci = dci;

	sys_mutex_init(&transfer_ring->mutex);

	status = usbman_xhci_producer_ring_init(&transfer_ring->ring);
	if (status != ferr_ok) {
		goto out;
	}

	transfer_ring->available_count = transfer_ring->ring.common.entry_count;

	sys_semaphore_init(&transfer_ring->transaction_reservation_semaphore, 1);

out:
	return status;
};

void usbman_xhci_transfer_ring_destroy(usbman_xhci_transfer_ring_t* transfer_ring) {
	usbman_xhci_producer_ring_destroy(&transfer_ring->ring);
};

ferr_t usbman_xhci_transfer_ring_produce(usbman_xhci_transfer_ring_t* transfer_ring, const usbman_xhci_trb_t* trb, usbman_xhci_producer_ring_callback_f callback, void* context) {
	usbman_xhci_trb_t modified_trb;
	ferr_t status = ferr_ok;

	eve_mutex_lock(&transfer_ring->mutex);

	if (transfer_ring->reserved_transaction_count == 0) {
		status = ferr_should_restart;
		goto out;
	}

	simple_memcpy(&modified_trb, trb, sizeof(modified_trb));

	// always set interrupt-on-completion
	// TODO: fix event handling so we don't need to do this
	modified_trb.control |= 1 << 5;

	status = usbman_xhci_producer_ring_produce(&transfer_ring->ring, &modified_trb, callback, context);
	if (status != ferr_ok) {
		goto out;
	}

	--transfer_ring->reserved_transaction_count;

	if (transfer_ring->reserved_transaction_count == 0) {
		transfer_ring->controller->doorbell_array[transfer_ring->slot_id] = usbman_xhci_doorbell_make(transfer_ring->dci, 0);

		sys_semaphore_up(&transfer_ring->transaction_reservation_semaphore);
	}

out:
	sys_mutex_unlock(&transfer_ring->mutex);
	return status;
};

ferr_t usbman_xhci_transfer_ring_notify_consume(usbman_xhci_transfer_ring_t* transfer_ring, const usbman_xhci_trb_t* completion_trb, usbman_xhci_trb_t* out_consumed_trb, usbman_xhci_producer_ring_callback_entry_t* out_callback_entry) {
	ferr_t status = ferr_ok;

	eve_mutex_lock(&transfer_ring->mutex);

	status = usbman_xhci_producer_ring_notify_consume(&transfer_ring->ring, completion_trb, out_consumed_trb, out_callback_entry);

	if (status == ferr_ok) {
		++transfer_ring->available_count;
	}

	sys_mutex_unlock(&transfer_ring->mutex);

	return status;
};

ferr_t usbman_xhci_transfer_ring_reserve_transaction(usbman_xhci_transfer_ring_t* transfer_ring, size_t trb_count, bool can_wait) {
	ferr_t status = ferr_ok;

	if (can_wait) {
		eve_semaphore_down(&transfer_ring->transaction_reservation_semaphore);
	} else {
		status = sys_semaphore_try_down(&transfer_ring->transaction_reservation_semaphore);
		if (status != ferr_ok) {
			goto out;
		}
	}

	eve_mutex_lock(&transfer_ring->mutex);

	if (transfer_ring->reserved_transaction_count > 0) {
		status = ferr_resource_unavailable;
		goto out;
	}

	if (transfer_ring->available_count < trb_count) {
		status = ferr_temporary_outage;
		goto out;
	}

	transfer_ring->available_count -= trb_count;
	transfer_ring->reserved_transaction_count = trb_count;

out:
	sys_mutex_unlock(&transfer_ring->mutex);
	return status;
};

static void usbman_xhci_interrupt_handler(void* context, pci_device_t* pci_device) {
	usbman_xhci_controller_t* controller = context;

	//sys_console_log("XHCI: interrupt handler triggered\n");

	// write the status back to itself to clear interrupt bits
	uint32_t status = controller->operational_registers->status;
	if ((status & usbman_xhci_controller_status_flag_host_controller_error) != 0) {
		sys_console_log_f("interrupt: host controller error\n");
		sys_abort();
	}
	if ((status & usbman_xhci_controller_status_flag_host_system_error) != 0) {
		sys_console_log_f("interrupt: host system error\n");
		sys_abort();
	}
	controller->operational_registers->status = status;

	// clear the interrupt pending bit by writing back the register to itself
	controller->runtime_registers->interrupter_register_sets[0].management = controller->runtime_registers->interrupter_register_sets[0].management;

	usbman_xhci_event_ring_schedule_poll(&controller->primary_event_ring);
};

USBMAN_STRUCT(usbman_xhci_device_request_context) {
	usbman_device_request_callback_f callback;
	void* context;
};

static void usbman_xhci_device_request_complete(void* context, const usbman_xhci_trb_t* consumed_trb, const usbman_xhci_trb_t* completion_trb) {
	usbman_xhci_device_request_context_t* request_context = context;
	usbman_request_status_t request_status = usbman_request_status_ok;
	if ((completion_trb->status >> 24) != usbman_xhci_trb_completion_code_success) {
		// TODO: add more details about the status
		request_status = usbman_request_status_unknown;
	}
	request_context->callback(request_context->context, request_status);
	USBMAN_WUR_IGNORE(sys_mempool_free(request_context));
};

USBMAN_ALWAYS_INLINE uintptr_t region_boundary(uintptr_t start, size_t length, uint8_t boundary_alignment_power) {
	if (boundary_alignment_power > 63) {
		return 0;
	}
	uintptr_t boundary_alignment_mask = (1ull << boundary_alignment_power) - 1;
	uintptr_t next_boundary = (start & ~boundary_alignment_mask) + (1ull << boundary_alignment_power);
	return (next_boundary > start && next_boundary < start + length) ? next_boundary : 0;
};

USBMAN_ALWAYS_INLINE uint8_t round_down_to_alignment_power(uint64_t byte_count) {
	if (byte_count == 0) {
		return 0;
	}
	return ferro_bits_in_use_u64(byte_count) - 1;
};

USBMAN_ALWAYS_INLINE uint8_t round_up_to_alignment_power(uint64_t byte_count) {
	uint8_t power = round_down_to_alignment_power(byte_count);
	return ((1ull << power) < byte_count) ? (power + 1) : power;
};

static ferr_t usbman_xhci_device_make_request(usbman_device_object_t* device, usbman_request_direction_t direction, usbman_request_type_t type, usbman_request_recipient_t recipient, usbman_request_code_t code, uint16_t value, uint16_t index, void* physical_data, uint16_t data_length, usbman_device_request_callback_f callback, void* context) {
	ferr_t status = ferr_ok;
	usbman_xhci_port_t* port = device->private_data;
	usbman_xhci_trb_t setup_stage;
	usbman_xhci_trb_t data_stage;
	usbman_xhci_trb_t status_stage;
	uint8_t request_type_bitmap = 0;
	usbman_xhci_device_request_context_t* request_context = NULL;

	if (data_length > 0 && !physical_data) {
		status = ferr_invalid_argument;
		goto out;
	}

	if (physical_data) {
		// make sure the data doesn't cross a 64KiB boundary
		if (region_boundary((uintptr_t)physical_data, data_length, round_up_to_alignment_power(64 * 1024)) != 0) {
			status = ferr_invalid_argument;
			goto out;
		}
	}

	status = sys_mempool_allocate(sizeof(*request_context), NULL, (void*)&request_context);
	if (status != ferr_ok) {
		goto out;
	}

	simple_memset(request_context, 0, sizeof(*request_context));

	request_context->callback = callback;
	request_context->context = context;

	simple_memset(&setup_stage, 0, sizeof(setup_stage));
	simple_memset(&data_stage, 0, sizeof(data_stage));
	simple_memset(&status_stage, 0, sizeof(status_stage));

	request_type_bitmap = (direction << 7) | (type << 5) | recipient;

	setup_stage.parameters[0] = (uint32_t)value << 16 | (uint32_t)code << 8 | request_type_bitmap;
	setup_stage.parameters[1] = (uint32_t)data_length << 16 | index;
	setup_stage.status = 8; // 8 byte transfer (always 8 bytes for setup), interrupter target = 0
	setup_stage.control = ((uint32_t)usbman_xhci_trb_type_setup_stage << 10) | usbman_xhci_transfer_flag_immediate_data;

	if (data_length == 0) {
		setup_stage.control |= (uint32_t)usbman_xhci_transfer_type_no_data_stage << 16;
	} else if (direction == usbman_request_direction_device_to_host) {
		setup_stage.control |= (uint32_t)usbman_xhci_transfer_type_in_data_stage << 16;
	} else {
		setup_stage.control |= (uint32_t)usbman_xhci_transfer_type_out_data_stage << 16;
	}

	if (data_length > 0) {
		data_stage.parameters[0] = (uintptr_t)physical_data & 0xffffffff;
		data_stage.parameters[1] = (uintptr_t)physical_data >> 32;
		data_stage.status = data_length; // <data_length> bytes to transfer, interrupter target = 0, td size = 0
		data_stage.control = ((uint32_t)usbman_xhci_trb_type_data_stage << 10);
		if (direction == usbman_request_direction_device_to_host) {
			data_stage.control |= 1 << 16; // direction = in
		} else {
			//data_stage.control |= 0 << 16; // direction = out
		}
	}

	//status_stage.status = 0; // interrupter target = 0
	status_stage.control = ((uint32_t)usbman_xhci_trb_type_status_stage << 10);
	if (direction == usbman_request_direction_device_to_host && data_length > 0) {
		//status_stage.control |= 0 << 16; // direction = out
	} else {
		status_stage.control |= 1 << 16; // direction = in
	}

	status = usbman_xhci_transfer_ring_reserve_transaction(&port->transfer_rings[0], (data_length > 0) ? 3 : 2, true);
	if (status != ferr_ok) {
		goto out;
	}

	// these should not fail now that we've successfully reserved a transaction
	sys_abort_status_log(usbman_xhci_transfer_ring_produce(&port->transfer_rings[0], &setup_stage, NULL, NULL));
	if (data_length > 0) {
		sys_abort_status_log(usbman_xhci_transfer_ring_produce(&port->transfer_rings[0], &data_stage, NULL, NULL));
	}
	sys_abort_status_log(usbman_xhci_transfer_ring_produce(&port->transfer_rings[0], &status_stage, usbman_xhci_device_request_complete, request_context));

out:
	if (status != ferr_ok) {
		if (request_context) {
			USBMAN_WUR_IGNORE(sys_mempool_free(request_context));
		}
	}
	return status;
};

USBMAN_STRUCT(usbman_xhci_device_configure_endpoint_context) {
	usbman_xhci_port_t* port;
	usbman_device_configure_endpoint_callback_f callback;
	void* context;
};

static void usbman_xhci_device_configure_endpoints_complete(void* context, const usbman_xhci_trb_t* consumed_trb, const usbman_xhci_trb_t* completion_trb) {
	usbman_xhci_device_configure_endpoint_context_t* configure_endpoint_context = context;
	ferr_t status = ferr_ok;
	if (configure_endpoint_context->port->temp) {
		USBMAN_WUR_IGNORE(sys_mempool_free(configure_endpoint_context->port->temp));
		configure_endpoint_context->port->temp = NULL;
	}
	if ((completion_trb->status >> 24) != usbman_xhci_trb_completion_code_success) {
		// TODO: add more details to the error
		status = ferr_unknown;
	}
	configure_endpoint_context->callback(configure_endpoint_context->context, status);
	USBMAN_WUR_IGNORE(sys_mempool_free(configure_endpoint_context));
};

static ferr_t usbman_xhci_device_configure_endpoints(usbman_device_object_t* device, const usbman_device_configure_endpoint_entry_t* entries, size_t entry_count, usbman_device_configure_endpoint_callback_f callback, void* context) {
	ferr_t status = ferr_ok;
	usbman_xhci_port_t* port = device->private_data;
	usbman_xhci_context_input_t* input_context = NULL;
	void* physical_temp = NULL;
	usbman_xhci_device_configure_endpoint_context_t* configure_endpoint_context = NULL;
	uint8_t context_entry_count = 0;

	status = sys_mempool_allocate(sizeof(*configure_endpoint_context), NULL, (void*)&configure_endpoint_context);
	if (status != ferr_ok) {
		goto out;
	}

	simple_memset(configure_endpoint_context, 0, sizeof(*configure_endpoint_context));

	configure_endpoint_context->port = port;
	configure_endpoint_context->callback = callback;
	configure_endpoint_context->context = context;

	status = sys_mempool_allocate_advanced(sizeof(*input_context), round_up_to_alignment_power(64), PAGE_ALIGNMENT, sys_mempool_flag_physically_contiguous, NULL, &port->temp);
	if (status != ferr_ok) {
		goto out;
	}

	status = sys_page_translate(port->temp, (void*)&physical_temp);
	if (status != ferr_ok) {
		goto out;
	}

	input_context = port->temp;

	simple_memset(input_context, 0, sizeof(*input_context));

	// drop all other (old) endpoints
	input_context->control.drop = ~3;

	input_context->control.add = 1;

	for (size_t i = 0; i < entry_count; ++i) {
		const usbman_device_configure_endpoint_entry_t* entry = &entries[i];
		uint8_t dci = (entry->endpoint_number * 2) + ((entry->direction == usbman_endpoint_direction_in) ? 1 : 0);
		bool inited_ring = false;
		usbman_xhci_endpoint_type_t ep_type = 0;

		if (dci > context_entry_count) {
			context_entry_count = dci;
		}

		switch (entry->type) {
			case usbman_endpoint_type_control:
				ep_type = usbman_xhci_endpoint_type_control;
				break;
			case usbman_endpoint_type_isochronous:
				ep_type = (entry->direction == usbman_endpoint_direction_in) ? usbman_xhci_endpoint_type_isoch_in : usbman_xhci_endpoint_type_isoch_out;
				break;
			case usbman_endpoint_type_bulk:
				ep_type = (entry->direction == usbman_endpoint_direction_in) ? usbman_xhci_endpoint_type_bulk_in : usbman_xhci_endpoint_type_bulk_out;
				break;
			case usbman_endpoint_type_interrupt:
				ep_type = (entry->direction == usbman_endpoint_direction_in) ? usbman_xhci_endpoint_type_interrupt_in : usbman_xhci_endpoint_type_interrupt_out;
				break;
		}

		status = usbman_xhci_transfer_ring_init(&port->transfer_rings[dci - 1], port->controller, port->slot, dci);
		if (status != ferr_ok) {
			goto out;
		}

		input_context->control.add |= 1 << dci;

		// TODO: add stream support

		// endpoint state = 0 (required for input), mult = 0, max primary streams = 0, linear stream array = 0, interval = <interval_power>, max esit payload hi = 0
		input_context->device.endpoints[dci - 1].fields[0] = (uint32_t)entry->interval_power << 16;

		// error count = 0 if isochronous or 3 otherwise, endpoint type = <ep_type>, host initiate disable = 0, max burst size = 0, max packet size = <max packet size>
		input_context->device.endpoints[dci - 1].fields[1] = ((entry->type == usbman_endpoint_type_isochronous ? 0 : 3) << 1) | ((uint32_t)ep_type << 3) | ((uint32_t)entry->max_packet_size << 16);

		// dequeue cycle state = 1, tr dequeue pointer low = <pointer low>
		input_context->device.endpoints[dci - 1].fields[2] = (1 << 0) | ((uintptr_t)port->transfer_rings[dci - 1].ring.common.physical_start & 0xffffffff);

		// tr dequeue pointer high = <pointer high>
		input_context->device.endpoints[dci - 1].fields[3] = (uintptr_t)port->transfer_rings[dci - 1].ring.common.physical_start >> 32;

		// average TRB length = sizeof(usbman_xhci_trb_t)
		input_context->device.endpoints[dci - 1].fields[4] = (sizeof(usbman_xhci_trb_t) & 0xffff);
	}

	// route string = 0, multi-tt disabled, not a hub, context entries = <context_entry_count>
	input_context->device.slot.fields[0] = context_entry_count << 27;

	// root hub port number = <port number>, number of ports = 0 (not a hub), max exit latency = 0? (not sure what to put here)
	input_context->device.slot.fields[1] = (uint32_t)port->port_number << 16;

	// parent hub slot id = 0 (root hub port), parent port number = 0 (root hub port), tt think time = 0 (not a hub), interrupter target = 0

	// usb device address = 0 (required for input), slot state = 0 (required for input)

	usbman_xhci_trb_t configure_endpoint_command;
	simple_memset(&configure_endpoint_command, 0, sizeof(configure_endpoint_command));
	configure_endpoint_command.parameters[0] = (uintptr_t)physical_temp & 0xffffffff;
	configure_endpoint_command.parameters[1] = (uintptr_t)physical_temp >> 32;
	configure_endpoint_command.control = ((uint32_t)usbman_xhci_trb_type_configure_endpoint_command << 10) | ((uint32_t)port->slot << 24);

	status = usbman_xhci_command_ring_produce(&port->controller->command_ring, &configure_endpoint_command, usbman_xhci_device_configure_endpoints_complete, configure_endpoint_context);
	if (status != ferr_ok) {
		goto out;
	}

out:
	if (status != ferr_ok) {
		// TODO: properly destroy rings on failure
#if 0
		if (inited_ring) {
			usbman_xhci_transfer_ring_destroy(&port->transfer_rings[dci - 1]);
		}
#endif

		if (port->temp) {
			USBMAN_WUR_IGNORE(sys_mempool_free(port->temp));
			port->temp = NULL;
		}

		if (configure_endpoint_context) {
			USBMAN_WUR_IGNORE(sys_mempool_free(configure_endpoint_context));
		}
	}

	return status;
};

USBMAN_STRUCT(usbman_xhci_device_perform_transfer_context) {
	usbman_device_perform_transfer_callback_f callback;
	void* context;
};

static void usbman_xhci_device_perform_transfer_complete(void* ctx, const usbman_xhci_trb_t* consumed_trb, const usbman_xhci_trb_t* completion_trb) {
	usbman_xhci_device_perform_transfer_context_t* transfer_context = ctx;
	ferr_t status = ferr_ok;
	usbman_xhci_trb_completion_code_t completion_code = (completion_trb->status >> 24);

	if (completion_code != usbman_xhci_trb_completion_code_success && completion_code != usbman_xhci_trb_completion_code_short_packet) {
		// TODO: add more details to the error
		status = ferr_unknown;
	}

	transfer_context->callback(transfer_context->context, status, (uint32_t)(consumed_trb->status & 0xffff) - (completion_trb->status & 0xffffff));
	USBMAN_WUR_IGNORE(sys_mempool_free(transfer_context));
};

static ferr_t usbman_xhci_device_perform_transfer(usbman_device_object_t* device, uint8_t endpoint_number, usbman_endpoint_direction_t direction, void* physical_data, uint16_t data_length, usbman_device_perform_transfer_callback_f callback, void* context) {
	ferr_t status = ferr_ok;
	uint8_t dci = (endpoint_number * 2) + ((direction == usbman_endpoint_direction_in) ? 1 : 0);
	usbman_xhci_trb_t trb;
	usbman_xhci_port_t* port = device->private_data;
	usbman_xhci_device_perform_transfer_context_t* transfer_context = NULL;

	trb.parameters[0] = (uintptr_t)physical_data & 0xffffffff;
	trb.parameters[1] = (uintptr_t)physical_data >> 32;
	trb.status = data_length; // interrupter target = 0, td size = 0, trb transfer length = <data_length>
	trb.control = (1 << 2) | ((uint32_t)usbman_xhci_trb_type_normal << 10); // interrupt on short packet and TRB type = normal

	status = sys_mempool_allocate(sizeof(*transfer_context), 0, (void*)&transfer_context);
	if (status != ferr_ok) {
		goto out;
	}

	simple_memset(transfer_context, 0, sizeof(*transfer_context));

	transfer_context->callback = callback;
	transfer_context->context = context;

	status = usbman_xhci_transfer_ring_reserve_transaction(&port->transfer_rings[dci - 1], 1, true);
	if (status != ferr_ok) {
		goto out;
	}

	status = usbman_xhci_transfer_ring_produce(&port->transfer_rings[dci - 1], &trb, usbman_xhci_device_perform_transfer_complete, transfer_context);
	if (status != ferr_ok) {
		goto out;
	}

out:
	if (status != ferr_ok) {
		if (transfer_context) {
			USBMAN_WUR_IGNORE(sys_mempool_free(transfer_context));
		}
	}
	return status;
};

static void usbman_xhci_port_evaluate_context_complete(void* context, const usbman_xhci_trb_t* consumed_trb, const usbman_xhci_trb_t* completion_trb) {
	usbman_xhci_port_t* port = context;

	if ((completion_trb->status >> 24) != usbman_xhci_trb_completion_code_success) {
		sys_console_log_f("XHCI: port #%u: evaluate_context command failed: %u\n", port->port_number, (completion_trb->status >> 24));
		sys_semaphore_up(&port->controller->init_semaphore);
		return;
	}

	sys_console_log_f("XHCI: port #%u: successfully updated max packet size\n", port->port_number);

	if (port->temp) {
		USBMAN_WUR_IGNORE(sys_mempool_free(port->temp));
		port->temp = NULL;
	}

	// we can now continue initializing other devices
	sys_semaphore_up(&port->controller->init_semaphore);
};

static void usbman_xhci_port_get_descriptor_complete(void* context, usbman_request_status_t request_status) {
	usbman_xhci_port_t* port = context;
	ferr_t status = ferr_ok;
	usbman_xhci_context_input_t* input_context = NULL;
	void* physical_temp = NULL;

	if (request_status != usbman_request_status_ok) {
		sys_console_log_f("XHCI: port #%u: get_descriptor request failed: %d\n", port->port_number, request_status);
		if (port->temp) {
			USBMAN_WUR_IGNORE(sys_mempool_free(port->temp));
			port->temp = NULL;
		}
		sys_semaphore_up(&port->controller->init_semaphore);
		return;
	}

	// NOTE: we only have the first 8 bytes of this structure right now
	usbman_device_descriptor_t* desc = port->temp;

	if ((desc->usb_version >> 8) == 2) {
		// this is a USB 2.0 device; the max packet size is a byte count
		port->max_packet_size = desc->endpoint_0_max_packet_size;
	} else {
		// this is a USB 3.0 device; the max packet size is an exponent of two
		port->max_packet_size = 1ull << desc->endpoint_0_max_packet_size;
	}

	sys_console_log_f("XHCI: port #%u: max packet size = %zu, device class = %u, device subclass = %u, device protocol = %u\n", port->port_number, port->max_packet_size, desc->device_class, desc->device_subclass, desc->device_protocol);

	USBMAN_WUR_IGNORE(sys_mempool_free(port->temp));
	port->temp = NULL;

	//
	// now update the max packet size
	//

	status = sys_mempool_allocate_advanced(sizeof(*input_context), round_up_to_alignment_power(64), PAGE_ALIGNMENT, sys_mempool_flag_physically_contiguous, NULL, &port->temp);
	if (status != ferr_ok) {
		goto out;
	}

	status = sys_page_translate(port->temp, (void*)&physical_temp);
	if (status != ferr_ok) {
		goto out;
	}

	input_context = port->temp;

	simple_memset(input_context, 0, sizeof(*input_context));

	input_context->control.add = 1 << 1;

	// endpoint state = 0 (required for input), mult = 0, max primary streams = 0, linear stream array = 0, interval = 0, max esit payload hi = 0

	// error count = 3, endpoint type = control, host initiate disable = 0, max burst size = 0, max packet size = <max packet size>
	input_context->device.endpoints[0].fields[1] = (3 << 1) | ((uint32_t)usbman_xhci_endpoint_type_control << 3) | ((port->max_packet_size & 0xffff) << 16);

	// dequeue cycle state = 1, tr dequeue pointer low = <pointer low>
	input_context->device.endpoints[0].fields[2] = (1 << 0) | ((uintptr_t)port->transfer_rings[0].ring.common.physical_start & 0xffffffff);

	// tr dequeue pointer high = <pointer high>
	input_context->device.endpoints[0].fields[3] = (uintptr_t)port->transfer_rings[0].ring.common.physical_start >> 32;

	// average TRB length = sizeof(usbman_xhci_trb_t)
	input_context->device.endpoints[0].fields[4] = (sizeof(usbman_xhci_trb_t) & 0xffff);

	usbman_xhci_trb_t evaluate_context_command;
	simple_memset(&evaluate_context_command, 0, sizeof(evaluate_context_command));
	evaluate_context_command.parameters[0] = (uintptr_t)physical_temp & 0xffffffff;
	evaluate_context_command.parameters[1] = (uintptr_t)physical_temp >> 32;
	evaluate_context_command.control = ((uint32_t)usbman_xhci_trb_type_evaluate_context_command << 10) | ((uint32_t)port->slot << 24);

	sys_console_log_f("XHCI: port #%u: going to issue Evaluate Context command\n", port->port_number);

	status = usbman_xhci_command_ring_produce(&port->controller->command_ring, &evaluate_context_command, usbman_xhci_port_evaluate_context_complete, port);
	if (status != ferr_ok) {
		goto out;
	}

out:
	if (status != ferr_ok) {
		sys_console_log_f("XHCI: port #%u: failed to update max packet size\n", port->port_number);

		if (port->temp) {
			USBMAN_WUR_IGNORE(sys_mempool_free(port->temp));
			port->temp = NULL;
		}

		sys_semaphore_up(&port->controller->init_semaphore);
	}
};

static void usbman_xhci_port_address_device_complete(void* context, const usbman_xhci_trb_t* command_trb, const usbman_xhci_trb_t* completion_trb) {
	usbman_xhci_port_t* port = context;
	ferr_t status = ferr_ok;
	void* physical_temp = NULL;

	if ((completion_trb->status >> 24) != usbman_xhci_trb_completion_code_success) {
		sys_console_log_f("XHCI: port #%u: address_device command failed: %u\n", port->port_number, (completion_trb->status >> 24));
		sys_semaphore_up(&port->controller->init_semaphore);
		return;
	}

	sys_console_log_f("XHCI: port #%u: successfully addressed device\n", port->port_number);

	if (port->temp) {
		USBMAN_WUR_IGNORE(sys_mempool_free(port->temp));
		port->temp = NULL;
	}

	port->device_address = port->output_device_context->slot.fields[3] & 0xff;

	// allocate a buffer for the get_descriptor request

	status = sys_mempool_allocate_advanced(8, 0, round_up_to_alignment_power(64 * 1024), sys_mempool_flag_physically_contiguous, NULL, &port->temp);
	if (status != ferr_ok) {
		goto out;
	}

	simple_memset(port->temp, 0, 8);

	status = sys_page_translate(port->temp, (void*)&physical_temp);
	if (status != ferr_ok) {
		goto out;
	}

	status = usbman_xhci_device_make_request(port->device, usbman_request_direction_device_to_host, usbman_request_type_standard, usbman_request_recipient_device, usbman_request_code_get_descriptor, (uint16_t)usbman_descriptor_type_device << 8 /* | 0 (descriptor index = 0) */, 0, physical_temp, 8, usbman_xhci_port_get_descriptor_complete, port);
	if (status != ferr_ok) {
		goto out;
	}

out:
	if (status != ferr_ok) {
		sys_console_log_f("XHCI: port #%u: failed to perform get_descriptor request\n", port->port_number);

		if (port->temp) {
			USBMAN_WUR_IGNORE(sys_mempool_free(port->temp));
			port->temp = NULL;
		}

		sys_semaphore_up(&port->controller->init_semaphore);
	}
};

static void usbman_xhci_port_enable_slot_complete(void* context, const usbman_xhci_trb_t* command_trb, const usbman_xhci_trb_t* completion_trb) {
	usbman_xhci_port_t* port = context;
	ferr_t status = ferr_ok;
	void* physical_input_context = NULL;
	usbman_xhci_context_input_t* input_context = NULL;
	bool inited_ring = false;
	bool inited_output = false;
	void* physical_output_device_context = NULL;
	uint16_t default_max_packet_size = 8;

	if ((completion_trb->status >> 24) != usbman_xhci_trb_completion_code_success) {
		sys_console_log_f("XHCI: port #%u: enable_slot command failed: %u\n", port->port_number, (completion_trb->status >> 24));
		status = ferr_unknown;
		goto out;
	}

	port->slot = completion_trb->control >> 24;
	port->controller->slots_to_ports[port->slot] = port->port_number;

	sys_console_log_f("XHCI: port #%u: got slot #%u\n", port->port_number, port->slot);

	status = sys_mempool_allocate_advanced(sizeof(*input_context), round_up_to_alignment_power(64), PAGE_ALIGNMENT, sys_mempool_flag_physically_contiguous, NULL, (void*)&input_context);
	if (status != ferr_ok) {
		goto out;
	}

	status = sys_page_translate(input_context, (void*)&physical_input_context);
	if (status != ferr_ok) {
		goto out;
	}

	simple_memset(input_context, 0, sizeof(*input_context));

	input_context->control.add = (1 << 0) | (1 << 1);

	// route string = 0, multi-tt disabled, not a hub, context entries = 1
	input_context->device.slot.fields[0] = 1 << 27;

	// root hub port number = <port number>, number of ports = 0 (not a hub), max exit latency = 0? (not sure what to put here)
	input_context->device.slot.fields[1] = (uint32_t)port->port_number << 16;

	// parent hub slot id = 0 (root hub port), parent port number = 0 (root hub port), tt think time = 0 (not a hub), interrupter target = 0

	// usb device address = 0 (required for input), slot state = 0 (required for input)

	status = usbman_xhci_transfer_ring_init(&port->transfer_rings[0], port->controller, port->slot, 1);
	if (status != ferr_ok) {
		goto out;
	}

	inited_ring = true;

	switch (port->speed_id) {
		case usbman_speed_id_low_speed:
			default_max_packet_size = 8;
			break;

		case usbman_speed_id_high_speed:
			default_max_packet_size = 64;
			break;

		case usbman_speed_id_super_speed_gen_1_x1:
		case usbman_speed_id_super_speed_plus_gen_1_x2:
		case usbman_speed_id_super_speed_plus_gen_2_x1:
		case usbman_speed_id_super_speed_plus_gen_2_x2:
			default_max_packet_size = 512;
			break;

		// full speed devices need to have their speed determined by reading the device descriptor
	}

	// endpoint state = 0 (required for input), mult = 0, max primary streams = 0, linear stream array = 0, interval = 0, max esit payload hi = 0

	// error count = 3, endpoint type = control, host initiate disable = 0, max burst size = 0, max packet size = <default_max_packet_size>
	input_context->device.endpoints[0].fields[1] = (3 << 1) | ((uint32_t)usbman_xhci_endpoint_type_control << 3) | (default_max_packet_size << 16);

	// dequeue cycle state = 1, tr dequeue pointer low = <pointer low>
	input_context->device.endpoints[0].fields[2] = (1 << 0) | ((uintptr_t)port->transfer_rings[0].ring.common.physical_start & 0xffffffff);

	// tr dequeue pointer high = <pointer high>
	input_context->device.endpoints[0].fields[3] = (uintptr_t)port->transfer_rings[0].ring.common.physical_start >> 32;

	// average TRB length = sizeof(usbman_xhci_trb_t)
	input_context->device.endpoints[0].fields[4] = (sizeof(usbman_xhci_trb_t) & 0xffff);

	status = sys_mempool_allocate_advanced(sizeof(*port->output_device_context), round_up_to_alignment_power(64), PAGE_ALIGNMENT, sys_mempool_flag_physically_contiguous, NULL, (void*)&port->output_device_context);
	if (status != ferr_ok) {
		goto out;
	}

	status = sys_page_translate(port->output_device_context, (void*)&physical_output_device_context);
	if (status != ferr_ok) {
		goto out;
	}

	simple_memset(port->output_device_context, 0, sizeof(*port->output_device_context));

	port->controller->device_context_base_address_array[port->slot].address = (uintptr_t)physical_output_device_context;

	inited_output = true;

	usbman_xhci_trb_t address_device_command;
	simple_memset(&address_device_command, 0, sizeof(address_device_command));
	address_device_command.parameters[0] = (uintptr_t)physical_input_context & 0xffffffff;
	address_device_command.parameters[1] = (uintptr_t)physical_input_context >> 32;
	// BSR = 0
	address_device_command.control = ((uint32_t)usbman_xhci_trb_type_address_device_command << 10) | ((uint32_t)port->slot << 24);

	port->temp = input_context;

	status = usbman_xhci_command_ring_produce(&port->controller->command_ring, &address_device_command, usbman_xhci_port_address_device_complete, port);
	if (status != ferr_ok) {
		goto out;
	}

out:
	if (status != ferr_ok) {
		sys_console_log_f("XHCI: port #%u: failed to issue address_device command\n", port->port_number);

		if (inited_output) {
			port->controller->device_context_base_address_array[port->slot].address = 0;
		}

		if (port->output_device_context) {
			USBMAN_WUR_IGNORE(sys_mempool_free(port->output_device_context));
			port->output_device_context = NULL;
		}

		if (inited_ring) {
			usbman_xhci_transfer_ring_destroy(&port->transfer_rings[0]);
		}

		if (input_context) {
			USBMAN_WUR_IGNORE(sys_mempool_free(input_context));
			port->temp = NULL;
		}

		sys_semaphore_up(&port->controller->init_semaphore);
	}
};

static usbman_speed_id_t usbman_xhci_device_get_standard_speed(usbman_device_object_t* device) {
	return ((usbman_xhci_port_t*)device->private_data)->speed_id;
};

static const usbman_device_methods_t xhci_device_methods = {
	.make_request = usbman_xhci_device_make_request,
	.configure_endpoints = usbman_xhci_device_configure_endpoints,
	.get_standard_speed = usbman_xhci_device_get_standard_speed,
	.perform_transfer = usbman_xhci_device_perform_transfer,
};

static const usbman_xhci_psi_array_entry_t* usbman_xhci_port_get_speed_info(usbman_xhci_port_t* port) {
	const usbman_xhci_psi_array_entry_t* final_entry = NULL;

	for (size_t i = 0; i < port->controller->port_speed_map_entry_count; ++i) {
		usbman_xhci_port_speed_entry_t* entry = &port->controller->port_speed_map[i];
		uint8_t psi = 0;

		if (port->port_number < entry->first_port_number || port->port_number > entry->last_port_number) {
			continue;
		}

		psi = usbman_xhci_port_get_speed(&port->controller->operational_registers->port_register_sets[port->port_number - 1]);
		final_entry = &entry->map[psi - 1];

		break;
	}

	return final_entry;
};

static uint8_t usbman_xhci_port_get_protocol_major_version(usbman_xhci_port_t* port) {
	uint8_t version = 0;

	for (size_t i = 0; i < port->controller->port_speed_map_entry_count; ++i) {
		usbman_xhci_port_speed_entry_t* entry = &port->controller->port_speed_map[i];

		if (port->port_number < entry->first_port_number || port->port_number > entry->last_port_number) {
			continue;
		}

		version = entry->major_version;

		break;
	}

	return version;
};

static uint8_t usbman_xhci_port_get_protocol_major_version_alt(usbman_xhci_controller_t* controller, uint8_t port_number) {
	uint8_t version = 0;

	for (size_t i = 0; i < controller->port_speed_map_entry_count; ++i) {
		usbman_xhci_port_speed_entry_t* entry = &controller->port_speed_map[i];
		uint8_t psi = 0;

		if (port_number < entry->first_port_number || port_number > entry->last_port_number) {
			continue;
		}

		version = entry->major_version;

		break;
	}

	return version;
};

static void usbman_xhci_scan_port(usbman_xhci_controller_t* controller, uint8_t port_number) {
	volatile usbman_xhci_port_register_set_t* port_regs = &controller->operational_registers->port_register_sets[port_number - 1];
	usbman_xhci_port_t* port = NULL;
	bool created = false;
	uint8_t protocol_version = 0;
	const usbman_xhci_psi_array_entry_t* speed_info = NULL;

	eve_semaphore_down(&controller->init_semaphore);

	if ((port_regs->status_and_control & usbman_xhci_port_status_and_control_flag_current_connect_status) == 0) {
		sys_semaphore_up(&controller->init_semaphore);
		return;
	}

	sys_console_log_f("XHCI: port #%u: device connected\n", port_number);

	protocol_version = usbman_xhci_port_get_protocol_major_version_alt(controller, port_number);

	if (protocol_version == 2) {
		// needs to be reset
		sys_console_log_f("XHCI: port #%u: resetting USB2 port...\n", port_number);
		port_regs->status_and_control = (port_regs->status_and_control & FUSB_XHCI_PORT_STATUS_AND_CONTROL_WRITE_PRESERVE_MASK) | usbman_xhci_port_status_and_control_flag_port_reset | usbman_xhci_port_status_and_control_flag_port_power;
	}

	// wait for it to be enabled
	sys_console_log_f("XHCI: port #%u: waiting for port to be enabled...\n", port_number);
	while ((port_regs->status_and_control & usbman_xhci_port_status_and_control_flag_port_enabled) == 0);

	eve_mutex_lock(&controller->ports_mutex);

	if (simple_ghmap_lookup_h(&controller->ports, port_number, true, sizeof(*port), &created, (void*)&port, NULL) != ferr_ok) {
		sys_mutex_unlock(&controller->ports_mutex);
		sys_console_log("XHCI: failed to allocate port structure\n");
		sys_semaphore_up(&controller->init_semaphore);
		return;
	}

	if (!created) {
		sys_mutex_unlock(&controller->ports_mutex);
		sys_console_log("XHCI: port structure already existed?\n");
		sys_semaphore_up(&controller->init_semaphore);
		return;
	}

	simple_memset(port, 0, sizeof(*port));

	port->controller = controller;
	port->port_number = port_number;

	if (usbman_device_new(controller->controller, &xhci_device_methods, port, &port->device) != ferr_ok) {
		USBMAN_WUR_IGNORE(simple_ghmap_clear_h(&controller->ports, port_number));
		sys_mutex_unlock(&controller->ports_mutex);
		sys_console_log("XHCI: failed to allocate device structure\n");
		sys_semaphore_up(&controller->init_semaphore);
		return;
	}

	speed_info = usbman_xhci_port_get_speed_info(port);
	port->speed_id = speed_info->standard_speed_id;
	port->bitrate = speed_info->bitrate;

	sys_mutex_unlock(&controller->ports_mutex);

	sys_console_log_f("XHCI: port #%u: standard speed = %u; bitrate = %llu bits/s\n", port->port_number, port->speed_id, port->bitrate);

	usbman_xhci_trb_t enable_slot_command;
	simple_memset(&enable_slot_command, 0, sizeof(enable_slot_command));
	enable_slot_command.control = (uint32_t)usbman_xhci_trb_type_enable_slot_command << 10;

	if (usbman_xhci_command_ring_produce(&controller->command_ring, &enable_slot_command, usbman_xhci_port_enable_slot_complete, port) != ferr_ok) {
		sys_console_log("XHCI: failed to issue enable_slot command\n");
		sys_semaphore_up(&controller->init_semaphore);
	}
};

static bool usbman_xhci_init_port_iterator(void* context, simple_ghmap_t* hashmap, simple_ghmap_hash_t hash, const void* key, size_t key_size, void* entry, size_t entry_size) {
	usbman_xhci_port_t* port = entry;

	// alright, now we can hand it over to the USB subsystem to configure and set up the device
	usbman_device_setup(port->device);

	return true;
};

#if XHCI_WATCHDOG
static void usbman_xhci_watchdog(void* context, sys_thread_t* this_thread) {
	usbman_xhci_controller_t* controller = context;

	while (true) {
		uint32_t status = controller->operational_registers->status;

		if ((status & usbman_xhci_controller_status_flag_host_controller_error) != 0) {
			sys_console_log("watchdog: host controller error\n");
			sys_abort();
		}

		if ((status & usbman_xhci_controller_status_flag_host_system_error) != 0) {
			sys_console_log("watchdog: host system error\n");
			sys_abort();
		}

		// sleep for 1 second
		USBMAN_WUR_IGNORE(sys_thread_suspend_timeout(sys_thread_current(), 1000000000, sys_timeout_type_relative_ns_monotonic));
	}
};
#endif

// STATIC ONLY FOR DEBUGGING PURPOSES
// DO NOT DEPEND ON THIS BEING A STATIC VARIABLE
// (i.e. it may become a local variable within usbman_xhci_init() later on)
static usbman_xhci_controller_t* controller = NULL;

static const usbman_controller_methods_t xhci_controller_methods = {};

static const uint16_t controller_ids[][2] = {
	// QEMU XHCI controller
	{ 0x1b36, 0x000d },

	{ 0x8086, 0x06ed },
	{ 0x8086, 0x34ed },

	// TODO: add more controller IDs
};

static bool pci_iterator(void* context, const pci_device_info_t* dev_info) {
	for (size_t i = 0; i < sizeof(controller_ids) / sizeof(*controller_ids); ++i) {
		if (dev_info->vendor_id == controller_ids[i][0] && dev_info->device_id == controller_ids[i][1]) {
			simple_memcpy(context, dev_info, sizeof(*dev_info));
			return false;
		}
	}
	return true;
};

#define MAX_PCI_CONNECT_TRIES 3

void usbman_xhci_init(void) {
	pci_device_t* dev = NULL;
	size_t dcbaa_size = 0;
	void* phys_dcbaa = NULL;
	uintptr_t phys_scratchpad_buffer_array;
	uint64_t scratchpad_count = 0;
	uint64_t controller_page_size = 0;
	sys_shared_memory_t* shared_mem = NULL;
	pci_device_info_t dev_info;

	if (pci_visit(pci_iterator, &dev_info) != ferr_cancelled) {
		sys_console_log("XHCI: controller not found\n");
		return;
	}

	for (size_t i = 0; i < MAX_PCI_CONNECT_TRIES; ++i) {
		if (pci_connect(&dev_info, &dev) == ferr_ok) {
			break;
		}
	}

	if (dev == NULL) {
		sys_console_log("XHCI: controller not found\n");
		return;
	}

	sys_console_log("XHCI: found controller\n");

	sys_abort_status_log(sys_mempool_allocate(sizeof(*controller), NULL, (void*)&controller));

	simple_memset(controller, 0, sizeof(*controller));

	sys_abort_status_log(usbman_controller_new(&xhci_controller_methods, controller, &controller->controller));

	sys_abort_status_log(simple_ghmap_init(&controller->ports, 16, 0, simple_ghmap_allocate_sys_mempool, simple_ghmap_free_sys_mempool, NULL, NULL, NULL, NULL, NULL, NULL));
	sys_mutex_init(&controller->ports_mutex);

	controller->device = dev;

	sys_semaphore_init(&controller->init_semaphore, 1);

	sys_abort_status_log(pci_device_register_interrupt_handler(controller->device, usbman_xhci_interrupt_handler, controller));

	sys_console_log("XHCI: registered interrupt handler\n");

	sys_abort_status_log(pci_device_get_mapped_bar(controller->device, 0, &shared_mem, &controller->bar0_size));
	sys_abort_status_log(sys_shared_memory_map(shared_mem, sys_page_round_up_count(controller->bar0_size), 0, (void*)&controller->capability_registers));
	sys_release(shared_mem);

	sys_console_log_f("XHCI: mapped BAR0 at %p, %zu bytes\n", controller->capability_registers, controller->bar0_size);

	controller->operational_registers = (volatile void*)((volatile char*)controller->capability_registers + usbman_xhci_controller_capability_registers_length(controller->capability_registers));
	controller->runtime_registers = (volatile void*)((volatile char*)controller->capability_registers + (controller->capability_registers->runtime_register_space_offset & ~0x1f));
	controller->doorbell_array = (volatile void*)((volatile char*)controller->capability_registers + (controller->capability_registers->doorbell_offset & ~3));
	controller->extended_capabilities_base = (volatile void*)((volatile char*)controller->capability_registers + (usbman_xhci_controller_capability_registers_extended_capabilities_pointer(controller->capability_registers) * sizeof(uint32_t)));

	sys_console_log_f("XHCI: cap=%p, op=%p, run=%p, db=%p\n", controller->capability_registers, controller->operational_registers, controller->runtime_registers, controller->doorbell_array);

	sys_abort_status_log(pci_device_enable_bus_mastering(controller->device));

	controller_page_size = (uint64_t)controller->operational_registers->page_size << 12;
	sys_console_log_f("XHCI: page size = %llu; supports 64-bit addresses? %s\n", controller_page_size, (controller->capability_registers->hcc_params_1 & usbman_xhci_controller_hcc_parameter_1_flag_is_64bit) != 0 ? "yes" : "no");

	//
	// let's find all port speed ID (PSI) mappings now
	//

	for (volatile uint32_t* xcap = controller->extended_capabilities_base; xcap != NULL; xcap = usbman_xhci_xcap_next(xcap)) {
		usbman_xhci_xcap_id_t xcap_id = usbman_xhci_xcap_get_id(xcap);

		//sys_console_log_f("XHCI: found xcap with ID %u\n", xcap_id);

		if (xcap_id == usbman_xhci_xcap_id_supported_protocol) {
			volatile usbman_xhci_xcap_supported_protocol_t* desc = (void*)xcap;
			uint8_t psi_count = desc->psic_and_compat_port_range >> 28;
			usbman_xhci_port_speed_entry_t* entry = NULL;

			sys_abort_status_log(sys_mempool_reallocate(controller->port_speed_map, sizeof(*controller->port_speed_map) * (controller->port_speed_map_entry_count + 1), NULL, (void*)&controller->port_speed_map));
			++controller->port_speed_map_entry_count;

			entry = &controller->port_speed_map[controller->port_speed_map_entry_count - 1];

			simple_memset(entry, 0, sizeof(*controller->port_speed_map));

			entry->first_port_number = desc->psic_and_compat_port_range & 0xff;
			entry->last_port_number = entry->first_port_number + ((desc->psic_and_compat_port_range >> 8) & 0xff) - 1;
			entry->major_version = (desc->header >> 24) & 0xff;
			entry->minor_version = (desc->header >> 16) & 0xff;

			if (psi_count > 0) {
				for (size_t i = 0; i < psi_count; ++i) {
					uint32_t psi = xcap[4 + i];
					uint64_t bit_rate = psi >> 16;
					uint8_t psi_exponent = (psi >> 4) & 3;
					uint8_t link_protocol = (psi >> 14) & 3;
					uint8_t psi_value = psi & 0x0f;

					switch (psi_exponent) {
						case 1:
							bit_rate *= 1000;
							break;
						case 2:
							bit_rate *= 1000000;
							break;
						case 3:
							bit_rate *= 1000000000;
							break;
					}

					// TODO: actually differentiate the different SuperSpeed Plus speeds

					// the standard USB speeds are *maximum* transfer speeds.
					// that's why we check for less-than-or-equal-to

					if (bit_rate <= 1500000) {
						entry->map[psi_value - 1].standard_speed_id = usbman_speed_id_low_speed;
					} else if (bit_rate <= 12000000) {
						entry->map[psi_value - 1].standard_speed_id = usbman_speed_id_full_speed;
					} else if (bit_rate <= 480000000) {
						entry->map[psi_value - 1].standard_speed_id = usbman_speed_id_high_speed;
					} else if (bit_rate <= 5000000000) {
						entry->map[psi_value - 1].standard_speed_id = usbman_speed_id_super_speed_gen_1_x1;
					} else if (bit_rate <= 10000000000) {
						entry->map[psi_value - 1].standard_speed_id = usbman_speed_id_super_speed_plus_gen_1_x2;
					} else {
						// unknown device speed?
						entry->map[psi_value - 1].standard_speed_id = usbman_speed_id_invalid;
					}

					entry->map[psi_value - 1].bitrate = bit_rate;
				}
			} else {
				// use implicit mappings
				for (uint8_t i = usbman_speed_id_full_speed; i <= usbman_speed_id_super_speed_plus_gen_2_x2; ++i) {
					entry->map[i - 1].standard_speed_id = i;
					entry->map[i - 1].bitrate = usbman_maximum_bitrates[i];
				}
			}
		} else if (xcap_id == usbman_xhci_xcap_id_legacy_support) {
			volatile usbman_xhci_xcap_legacy_support_t* desc = (void*)xcap;

			if ((desc->os_semaphore & 1) == 0 || (desc->bios_semaphore & 1) != 0) {
				sys_console_log("XHCI: controller not currently owned by OS; requesting ownership...\n");

				desc->os_semaphore |= 1;

				while ((desc->os_semaphore & 1) == 0 || (desc->bios_semaphore & 1) != 0);

				sys_console_log("XHCI: successfully acquired ownership of controller\n");
			}
		}
	}

	sys_console_log_f("XHCI: found %zu speed mappings\n", controller->port_speed_map_entry_count);

	sys_console_log("XHCI: halting host controller...\n");

	// halt the host controller (and disable interrupts)
	controller->operational_registers->command &= ~(usbman_xhci_controller_command_flag_run | usbman_xhci_controller_command_flag_interrupter_enable);

	while ((controller->operational_registers->status & usbman_xhci_controller_status_flag_host_controller_halted) == 0);

	sys_console_log("XHCI: host controller halted\n");

	sys_console_log("XHCI: resetting host controller...\n");

	// reset the host controller
	controller->operational_registers->command |= usbman_xhci_controller_command_flag_host_controller_reset;

	// wait for a bit to give it a chance to reset
	// 1ms should be enough
	USBMAN_WUR_IGNORE(sys_thread_suspend_timeout(sys_thread_current(), 1000000, sys_timeout_type_relative_ns_monotonic));

	while ((controller->operational_registers->command & usbman_xhci_controller_command_flag_host_controller_reset) != 0);
	while ((controller->operational_registers->status & usbman_xhci_controller_status_flag_controller_not_ready) != 0);

	sys_console_log("XHCI: host controller reset\n");

	// enable all device slots; disable U3 entry assertion; disable config info in Input Control Contexts
	sys_console_log_f("XHCI: max device slots = %u\n", usbman_xhci_controller_capability_registers_max_device_slots(controller->capability_registers));
	controller->operational_registers->configure = usbman_xhci_controller_capability_registers_max_device_slots(controller->capability_registers);

	scratchpad_count = usbman_xhci_controller_capability_registers_max_scratchpad_buffers(controller->capability_registers);
	sys_console_log_f("XHCI: max scratchpad buffers = %llu\n", scratchpad_count);

	//
	// allocate scratchpad buffer array
	//

	if (scratchpad_count > 0) {
		// TODO: mempool needs a way to indicate the mapping should be marked as uncacheable
		sys_abort_status_log(sys_mempool_allocate_advanced(sizeof(usbman_xhci_scratchpad_buffer_array_entry_t) * scratchpad_count, round_up_to_alignment_power(64), round_up_to_alignment_power(controller_page_size), sys_mempool_flag_physically_contiguous, NULL, (void*)&controller->scratchpad_buffer_array));
		sys_abort_status_log(sys_page_translate((void*)controller->scratchpad_buffer_array, (void*)&phys_scratchpad_buffer_array));

		sys_abort_status_log(sys_mempool_allocate(sizeof(*controller->virtual_scratchpad_buffer_array) * scratchpad_count, NULL, (void*)&controller->virtual_scratchpad_buffer_array));

		// clear out the array
		simple_memset((void*)controller->scratchpad_buffer_array, 0, sizeof(usbman_xhci_scratchpad_buffer_array_entry_t) * scratchpad_count);
	}

	// now allocate scratchpad buffers
	for (size_t i = 0; i < scratchpad_count; ++i) {
		void* phys_buffer = NULL;
		void* mapped_buffer = NULL;

		// use the page allocator, since it's more efficient at allocating entire pages

		sys_abort_status_log(sys_page_allocate(sys_page_round_up_count(controller_page_size), sys_page_flag_contiguous | sys_page_flag_prebound | sys_page_flag_uncacheable, &mapped_buffer));
		sys_abort_status_log(sys_page_translate(mapped_buffer, (void*)&phys_buffer));

		// clear out the buffer
		simple_memset(mapped_buffer, 0, controller_page_size);

		// assign it into the array
		controller->scratchpad_buffer_array[i] = (uintptr_t)phys_buffer;
		controller->virtual_scratchpad_buffer_array[i] = mapped_buffer;
	}

	//
	// allocate and configure the device context base address array
	//

	dcbaa_size = sizeof(usbman_xhci_device_context_base_address_entry_t) * (1 + usbman_xhci_controller_capability_registers_max_device_slots(controller->capability_registers));

	sys_abort_status_log(sys_page_allocate(sys_page_round_up_count(dcbaa_size), sys_page_flag_contiguous | sys_page_flag_prebound | sys_page_flag_uncacheable, (void*)&controller->device_context_base_address_array));
	sys_abort_status_log(sys_page_translate((void*)controller->device_context_base_address_array, (void*)&phys_dcbaa));

	// clear out the array
	simple_memset((void*)controller->device_context_base_address_array, 0, dcbaa_size);

	// assign the scratchpad buffer array address into the first entry
	controller->device_context_base_address_array[0].address = phys_scratchpad_buffer_array;

	controller->operational_registers->device_context_base_address_array_pointer = (uintptr_t)phys_dcbaa;

	//
	// allocate and configure the command ring
	//

	sys_abort_status_log(usbman_xhci_command_ring_init(&controller->command_ring, controller));

	controller->operational_registers->command_ring_control = (uintptr_t)controller->command_ring.ring.common.physical_start | usbman_xhci_command_ring_control_flag_ring_cycle_state;

	//
	// initialize the first interrupter
	//
	// we only use one interrupter for now
	//

	sys_abort_status_log(usbman_xhci_event_ring_init(&controller->primary_event_ring, &controller->runtime_registers->interrupter_register_sets[0].event_ring_dequeue_pointer, controller));

	controller->runtime_registers->interrupter_register_sets[0].event_ring_segment_table_size = 1;
	controller->runtime_registers->interrupter_register_sets[0].event_ring_dequeue_pointer = (uintptr_t)controller->primary_event_ring.ring.physical_dequeue;
	controller->runtime_registers->interrupter_register_sets[0].event_ring_segment_table_base_address = (uintptr_t)controller->primary_event_ring.physical_table;

	// leave the default interrupt interval

	// enable the interrupter
	controller->runtime_registers->interrupter_register_sets[0].management |= 1 << 1;

	//
	// now enable interrupts
	//

	controller->operational_registers->command |= usbman_xhci_controller_command_flag_interrupter_enable;

	//
	// let's turn on the controller
	//

	sys_console_log("XHCI: turning on controller...\n");

	controller->operational_registers->command |= usbman_xhci_controller_command_flag_run;

	// now wait for it to be un-halted
	while ((controller->operational_registers->status & usbman_xhci_controller_status_flag_host_controller_halted) != 0);

	sys_console_log("XHCI: turned on controller\n");

#if XHCI_WATCHDOG
	{
		// start a watchdog thread

		sys_abort_status_log(sys_thread_create(NULL, 2ull * 1024 * 1024, usbman_xhci_watchdog, controller, sys_thread_flag_resume, NULL));
	}
#endif

	// let's scan all ports for any devices that may be currently connected

	uint8_t max_port = usbman_xhci_controller_capability_registers_max_ports(controller->capability_registers);
	for (uint8_t i = 1; i <= max_port; ++i) {
		usbman_xhci_scan_port(controller, i);
	}

	eve_mutex_lock(&controller->ports_mutex);
	simple_ghmap_for_each(&controller->ports, usbman_xhci_init_port_iterator, NULL);
	sys_mutex_unlock(&controller->ports_mutex);
};
