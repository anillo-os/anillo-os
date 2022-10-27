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

#include <ferro/core/threads.h>
#include <ferro/core/console.h>
#include <ferro/core/panic.h>
#include <ferro/core/mempool.h>
#include <libsimple/libsimple.h>
#include <ferro/core/channels.h>
#include <ferro/core/paging.h>
#include <ferro/core/scheduler.h>

#define CHANNEL_TESTING 0

#if CHANNEL_TESTING
FERRO_STRUCT_FWD(ferro_testing_client);

FERRO_ENUM(uint64_t, ferro_testing_server_event) {
	ferro_testing_server_event_client_arrival = 1 << 0,
	ferro_testing_server_event_client_event = 1 << 1,
};

FERRO_ENUM(uint64_t, ferro_testing_client_event) {
	ferro_testing_client_event_message_arrival = 1 << 0,
	ferro_testing_client_event_peer_closure = 1 << 1,
};

FERRO_STRUCT(ferro_testing_server) {
	fchannel_server_t* server;
	ferro_testing_client_t** clients;
	size_t client_count;
	size_t client_array_size;
	flock_semaphore_t event_loop_semaphore;
	ferro_testing_server_event_t events;
	fwaitq_waiter_t client_arrival_waiter;
};

FERRO_STRUCT(ferro_testing_client) {
	ferro_testing_server_t* server;
	fchannel_t* channel;
	ferro_testing_client_event_t events;
	fwaitq_waiter_t message_arrival_waiter;
	fwaitq_waiter_t peer_closure_waiter;
};

static void ferro_testing_server_client_arrival(void* context) {
	ferro_testing_server_t* server = context;
	server->events |= ferro_testing_server_event_client_arrival;
	flock_semaphore_up(&server->event_loop_semaphore);
};

static void ferro_testing_client_message_arrival(void* context) {
	ferro_testing_client_t* client = context;
	client->events |= ferro_testing_client_event_message_arrival;
	client->server->events |= ferro_testing_server_event_client_event;
	flock_semaphore_up(&client->server->event_loop_semaphore);
};

static void ferro_testing_client_peer_closure(void* context) {
	ferro_testing_client_t* client = context;
	client->events |= ferro_testing_client_event_peer_closure;
	client->server->events |= ferro_testing_server_event_client_event;
	flock_semaphore_up(&client->server->event_loop_semaphore);
};

static void ferro_testing_server_thread(void* context) {
	flock_semaphore_t* server_start_semaphore = context;
	ferro_testing_server_t server = {0};

	flock_semaphore_init(&server.event_loop_semaphore, 0);

	fpanic_status(fchannel_server_new(&server.server));

	fpanic_status(fchannel_realm_publish(fchannel_realm_global(), "org.anillo.test-server", sizeof("org.anillo.test-server") - 1, server.server));

	fwaitq_waiter_init(&server.client_arrival_waiter, ferro_testing_server_client_arrival, &server);
	fwaitq_wait(&server.server->client_arrival_waitq, &server.client_arrival_waiter);

	// the server is now ready to begin accepting clients
	flock_semaphore_up(server_start_semaphore);

	while (true) {
		flock_semaphore_down(&server.event_loop_semaphore);

		if (server.events == 0) {
			continue;
		}

		if ((server.events & ferro_testing_server_event_client_arrival) != 0) {
			server.events &= ~ferro_testing_server_event_client_arrival;

			while (true) {
				ferro_testing_client_t* client = NULL;

				if (server.client_array_size < server.client_count + 1) {
					if (fmempool_reallocate(server.clients, sizeof(*server.clients) * (server.client_count + 1), NULL, (void*)&server.clients) != ferr_ok) {
						break;
					}

					++server.client_array_size;
				}

				simple_memset(&server.clients[server.client_count], 0, sizeof(*server.clients));

				if (fmempool_allocate(sizeof(*client), NULL, (void*)&client) != ferr_ok) {
					break;
				}

				simple_memset(client, 0, sizeof(*client));

				client->server = &server;
				fwaitq_waiter_init(&client->message_arrival_waiter, ferro_testing_client_message_arrival, client);
				fwaitq_waiter_init(&client->peer_closure_waiter, ferro_testing_client_peer_closure, client);

				if (fchannel_server_accept(server.server, fserver_channel_accept_flag_no_wait, &client->channel) != ferr_ok) {
					FERRO_WUR_IGNORE(fmempool_free(client));
					break;
				}

				fwaitq_wait(&client->channel->message_arrival_waitq, &client->message_arrival_waiter);
				fwaitq_wait(&fchannel_peer(client->channel, false)->close_waitq, &client->peer_closure_waiter);

				// immediately mark it as having messages, just so that we check
				client->events |= ferro_testing_client_event_message_arrival;
				server.events |= ferro_testing_server_event_client_event;

				server.clients[server.client_count] = client;
				++server.client_count;
			}
		}

		if ((server.events & ferro_testing_server_event_client_event) != 0) {
			server.events &= ~ferro_testing_server_event_client_event;

			for (size_t i = 0; i < server.client_count; ++i) {
				ferro_testing_client_t* client = server.clients[i];

				if (client->events == 0) {
					continue;
				}

				if ((client->events & ferro_testing_client_event_peer_closure) != 0) {
					// stop waiting for messages
					fwaitq_unwait(&client->channel->message_arrival_waitq, &client->message_arrival_waiter);

					// close our end of the channel
					FERRO_WUR_IGNORE(fchannel_close(client->channel));

					// and release it
					fchannel_release(client->channel);

					// now go ahead and delete our client context
					FERRO_WUR_IGNORE(fmempool_free(client));

					// and remove it from the client array
					simple_memmove(&server.clients[i], &server.clients[i + 1], server.client_count - i - 1);
					--server.client_count;

					// because we moved the other clients forward, we need to check this index again
					--i;

					continue;
				}

				if ((client->events & ferro_testing_client_event_message_arrival) != 0) {
					client->events &= ~ferro_testing_client_event_message_arrival;

					// listen for another message
					fwaitq_wait(&client->channel->message_arrival_waitq, &client->message_arrival_waiter);

					while (true) {
						fchannel_message_t incoming;
						fchannel_message_t outgoing;

						if (fchannel_receive(client->channel, fchannel_receive_flag_no_wait, &incoming) != ferr_ok) {
							break;
						}

						fconsole_logf("server got: %.*s\n", (int)incoming.body_length, (const char*)incoming.body);

						simple_memset(&outgoing, 0, sizeof(outgoing));

						outgoing.conversation_id = incoming.conversation_id;
						outgoing.body_length = incoming.body_length + 7;

						if (fmempool_allocate(outgoing.body_length, NULL, &outgoing.body) != ferr_ok) {
							fchannel_message_destroy(&incoming);
							break;
						}

						simple_memcpy(outgoing.body, "echo = ", 7);
						simple_memcpy(&outgoing.body[7], incoming.body, incoming.body_length);

						if (fchannel_send(client->channel, fchannel_send_flag_no_wait, &outgoing) != ferr_ok) {
							fchannel_message_destroy(&incoming);
							fchannel_message_destroy(&outgoing);
							break;
						}

						fchannel_message_destroy(&incoming);
						// do NOT destroy the outgoing message; that's owned by the channel now
					}
				}
			}
		}
	}
};
#endif

void ferro_testing_entry(void) {
	ferr_t status = ferr_ok;

#if CHANNEL_TESTING
	fthread_t* server_thread = NULL;
	flock_semaphore_t server_start_semaphore;
	fchannel_server_t* server = NULL;
	fchannel_t* client = NULL;
	fchannel_message_t outgoing;
	fchannel_message_t incoming;

	flock_semaphore_init(&server_start_semaphore, 0);

	fpanic_status(fthread_new(ferro_testing_server_thread, &server_start_semaphore, NULL, FPAGE_LARGE_PAGE_SIZE, 0, &server_thread));
	fpanic_status(fsched_manage(server_thread));
	fpanic_status(fthread_resume(server_thread));

	fthread_release(server_thread);

	// wait for the server to start
	flock_semaphore_down(&server_start_semaphore);

	fpanic_status(fchannel_realm_lookup(fchannel_realm_global(), "org.anillo.test-server", sizeof("org.anillo.test-server") - 1, &server));

	fpanic_status(fchannel_connect(server, 0, &client));

	fchannel_server_release(server);

	simple_memset(&outgoing, 0, sizeof(outgoing));

	outgoing.body_length = sizeof("hello!") - 1;
	fpanic_status(fmempool_allocate(outgoing.body_length, NULL, &outgoing.body));

	simple_memcpy(outgoing.body, "hello!", outgoing.body_length);

	fpanic_status(fchannel_send(client, 0, &outgoing));

	fpanic_status(fchannel_receive(client, 0, &incoming));

	fconsole_logf("client got back: %.*s\n", (int)incoming.body_length, (const char*)incoming.body);

	fchannel_message_destroy(&incoming);

	FERRO_WUR_IGNORE(fchannel_close(client));
	fchannel_release(client);
#endif
};
