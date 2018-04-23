#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <assert.h>
#include <libinput.h>
#include <string.h>
#include <stdlib.h>
#include <wayland-server.h>
#include <wlr/config.h>
#include <wlr/types/wlr_seat.h>

#include "tablet-unstable-v2-protocol.h"


struct wlr_tablet_manager_v2 {
	struct wl_global *wl_global;
	struct wl_list clients; // wlr_tablet_manager_client_v2::link

	struct wl_listener display_destroy;

	void *data;
};

struct wlr_tablet_manager_client_v2 {
	struct wl_list link;
	struct wl_client *client;
	struct wl_resource *resource;

	struct wlr_tablet_manager_v2 *manager;

	struct wl_list tablet_seats; // wlr_tablet_seat_v2::link
};

struct wlr_tablet_seat_v2 {
	struct wl_list link;
	struct wl_client *wl_client;
	struct wl_resource *resource;

	struct wlr_tablet_manager_client_v2 *client;
	struct wlr_seat_client *seat;

	struct wl_listener seat_destroy;
	struct wl_listener client_destroy;

	struct wl_list tools;
	struct wl_list tablets;
	struct wl_list pads; //wlr_tablet_pad_client_v2::link
};

struct wlr_tablet_client_v2 {
	struct wl_list link;
	struct wl_client *client;
	struct wl_resource *resource;

	struct wl_listener device_destroy;
	struct wl_listener client_destroy;
};

struct wlr_tablet_tool_client_v2 {
	struct wl_list link;
	struct wl_client *client;
	struct wl_resource *resource;

	struct wlr_surface *cursor;
	struct wl_listener cursor_destroy;

	struct wl_listener tool_destroy;
	struct wl_listener client_destroy;
};

struct wlr_tablet_pad_client_v2 {
	struct wl_list link;
	struct wl_client *client;
	struct wl_resource *resource;

	size_t button_count;

	size_t ring_cout;
	struct wl_resource **rings;

	size_t strip_cout;
	struct wl_resource **strips;

	struct wl_listener device_destroy;
	struct wl_listener client_destroy;
};

void wlr_tablet_v2_destroy(struct wlr_tablet_manager_v2 *manager);
static struct wlr_tablet_manager_client_v2 *tablet_manager_client_from_resource(struct wl_resource *resource);

static void tablet_seat_destroy(struct wl_client *client,
		struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static struct zwp_tablet_seat_v2_interface seat_impl = {
	.destroy = tablet_seat_destroy,
};

static struct wlr_tablet_seat_v2 *tablet_seat_from_resource (
		struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource, &zwp_tablet_seat_v2_interface,
		&seat_impl));
	return wl_resource_get_user_data(resource);
}

static void wlr_tablet_seat_v2_destroy(struct wl_resource *resource) {
	struct wlr_tablet_seat_v2 *seat = tablet_seat_from_resource(resource);

	seat->resource = NULL;
	/* We can't just destroy the struct, because we may need to iterate it
	 * on display->destroy/manager_destroy
	 */
	// TODO: Implement the free() check
}

static void handle_seat_destroy(struct wl_listener *listener, void *data) {
	struct wlr_tablet_seat_v2 *seat =
		wl_container_of(listener, seat, seat_destroy);

	seat->seat = NULL;
	wl_list_remove(&seat->seat_destroy.link);
	/* Remove leaves it in a defunct state, we will remove again in the
	 * actual destroy sequence
	 */
	wl_list_init(&seat->seat_destroy.link);
}

static void tablet_manager_destroy(struct wl_client *client,
		struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static void get_tablet_seat(struct wl_client *wl_client, struct wl_resource *resource,
		uint32_t id, struct wl_resource *seat_resource)
{
	struct wlr_tablet_manager_client_v2 *manager = tablet_manager_client_from_resource(resource);
	struct wlr_seat_client *seat = wlr_seat_client_from_resource(seat_resource);

	struct wlr_tablet_seat_v2 *tablet_seat =
		calloc(1, sizeof(struct wlr_tablet_seat_v2));
	if (tablet_seat == NULL) {
		wl_client_post_no_memory(wl_client);
		return;
	}

	tablet_seat->resource =
		wl_resource_create(wl_client, &zwp_tablet_seat_v2_interface, 1, id);
	if (tablet_seat->resource == NULL) {
		free(tablet_seat);
		wl_client_post_no_memory(wl_client);
		return;
	}


	tablet_seat->seat = seat;
	tablet_seat->client = manager;

	tablet_seat->seat_destroy.notify = handle_seat_destroy;
	wl_signal_add(&seat->events.destroy, &tablet_seat->seat_destroy);

	wl_resource_set_implementation(tablet_seat->resource, &seat_impl, tablet_seat,
		wlr_tablet_seat_v2_destroy);
	wl_list_insert(&manager->tablet_seats, &tablet_seat->link);
}

static struct zwp_tablet_manager_v2_interface manager_impl = {
	.get_tablet_seat = get_tablet_seat,
	.destroy = tablet_manager_destroy,
};

static struct wlr_tablet_manager_client_v2 *tablet_manager_client_from_resource (
		struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource, &zwp_tablet_manager_v2_interface,
		&manager_impl));
	return wl_resource_get_user_data(resource);
}

static void wlr_tablet_manager_v2_destroy(struct wl_resource *resource) {
	struct wlr_tablet_manager_client_v2 *client = tablet_manager_client_from_resource(resource);

	client->resource = NULL;
	/* We can't just destroy the struct, because we may need to iterate it
	 * on display->destroy/manager_destroy
	 */
	// TODO: Implement the free() check
}

static void tablet_v2_bind(struct wl_client *wl_client, void *data,
		uint32_t version, uint32_t id) {
	struct wlr_tablet_manager_v2 *manager = data;
	assert(wl_client && manager);

	struct wlr_tablet_manager_client_v2 *client =
		calloc(1, sizeof(struct wlr_tablet_manager_client_v2));
	if (client == NULL) {
		wl_client_post_no_memory(wl_client);
		return;
	}

	wl_list_init(&client->tablet_seats);

	client->resource =
		wl_resource_create(wl_client, &zwp_tablet_manager_v2_interface, version, id);
	if (client->resource == NULL) {
		free(client);
		wl_client_post_no_memory(wl_client);
		return;
	}
	client->client = wl_client;
	client->manager = manager;

	wl_resource_set_implementation(client->resource, &manager_impl, client,
		wlr_tablet_manager_v2_destroy);
	wl_list_insert(&manager->clients, &client->link);
}

static void handle_display_destroy(struct wl_listener *listener, void *data) {
	struct wlr_tablet_manager_v2 *tablet =
		wl_container_of(listener, tablet, display_destroy);
	wlr_tablet_v2_destroy(tablet);
}

void wlr_tablet_v2_destroy(struct wlr_tablet_manager_v2 *manager) {
	struct wlr_tablet_manager_client_v2 *tmp;
	struct wlr_tablet_manager_client_v2 *pos;

	wl_list_for_each_safe(pos, tmp, &manager->clients, link) {
		wl_resource_destroy(pos->resource);
	}

	wl_global_destroy(manager->wl_global);
	free(manager);
}

struct wlr_tablet_manager_v2 *wlr_tablet_v2_create(struct wl_display *display) {
	struct wlr_tablet_manager_v2 *tablet =
		calloc(1, sizeof(struct wlr_tablet_manager_v2));
	if (!tablet) {
		return NULL;
	}

	wl_list_init(&tablet->clients);

	tablet->display_destroy.notify = handle_display_destroy;
	wl_display_add_destroy_listener(display, &tablet->display_destroy);

	tablet->wl_global = wl_global_create(display,
		&zwp_tablet_manager_v2_interface, 1, tablet, tablet_v2_bind);
	if (tablet->wl_global == NULL) {
		free(tablet);
		return NULL;
	}

	return tablet;
}
