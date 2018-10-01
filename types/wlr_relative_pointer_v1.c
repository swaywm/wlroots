#include <stdlib.h>
#include <assert.h>
#include <inttypes.h>
#include <wlr/util/log.h>
#include <util/signal.h>
#include <wlr/types/wlr_relative_pointer_v1.h>
#include <wlr/types/wlr_seat.h>
#include "wayland-util.h"
#include "wayland-server.h"
#include "relative-pointer-unstable-v1-protocol.h"

static const struct zwp_relative_pointer_manager_v1_interface relative_pointer_manager_v1_impl;
static const struct zwp_relative_pointer_v1_interface relative_pointer_v1_impl;


/**
 * Callback functions
 */

static void relative_pointer_manager_v1_handle_resource_destroy(struct wl_resource *resource) {
	wl_list_remove(wl_resource_get_link(resource));
}


static void relative_pointer_v1_handle_resource_destroy(struct wl_resource *resource) {
	wl_list_remove(wl_resource_get_link(resource));

	struct wlr_relative_pointer_v1 *relative_pointer = wl_resource_get_user_data(resource);
	free(relative_pointer);
}


static void relative_pointer_manager_v1_handle_destroy(struct wl_client *client,
		struct wl_resource *resource) {
	wl_resource_destroy(resource);

	wlr_log(WLR_DEBUG, "relative_pointer_v1 manager unbound from client %p",
		client);
}


static void relative_pointer_manager_v1_handle_get_relative_pointer(struct wl_client *client,
		struct wl_resource *resource, uint32_t id, struct wl_resource *pointer) {
	struct wlr_seat_client *seat_client =
		wlr_seat_client_from_pointer_resource(pointer);
	assert(seat_client->client == client);

	struct wlr_relative_pointer_v1 *relative_pointer =
		calloc(1, sizeof(struct wlr_relative_pointer_v1));
	if (relative_pointer == NULL) {
		wl_client_post_no_memory(client);
		return;
	}

	struct wl_resource *relative_pointer_resource = wl_resource_create(client,
		&zwp_relative_pointer_v1_interface, wl_resource_get_version(resource), id);
	if (relative_pointer_resource == NULL) {
		free(relative_pointer);
		wl_client_post_no_memory(client);
		return;
	}

	relative_pointer->client = client;
	relative_pointer->resource = relative_pointer_resource;
	relative_pointer->pointer = (struct wl_pointer *) pointer;

	wl_signal_init(&relative_pointer->events.destroy);

	wl_list_insert(&seat_client->relative_pointers,
		wl_resource_get_link(relative_pointer_resource));

	wl_resource_set_implementation(relative_pointer_resource, &relative_pointer_v1_impl,
		relative_pointer, relative_pointer_v1_handle_resource_destroy);

	struct wlr_relative_pointer_manager_v1 *relative_pointer_manager =
		wl_resource_get_user_data(resource);
	wlr_signal_emit_safe(&relative_pointer_manager->events.new_relative_pointer,
		relative_pointer);

	wlr_log(WLR_DEBUG, "relative_pointer_v1 %p created for client %p",
		relative_pointer, client);
}


static void relative_pointer_v1_handle_destroy(struct wl_client *client,
		struct wl_resource *resource) {
	struct wlr_relative_pointer_v1 *relative_pointer =
		wl_resource_get_user_data(resource);
	wlr_log(WLR_DEBUG, "relative_pointer_v1 %p released by client %p",
		relative_pointer, client);

	wl_resource_destroy(resource);
}


static void relative_pointer_manager_v1_bind(struct wl_client *wl_client, void *data,
		uint32_t version, uint32_t id) {
	struct wlr_relative_pointer_manager_v1 *relative_pointer_manager = data;

	struct wl_resource *manager_resource = wl_resource_create(wl_client,
		&zwp_relative_pointer_manager_v1_interface, version, id);

	if (manager_resource == NULL) {
		wl_client_post_no_memory(wl_client);
		return;
	}

	wl_list_insert(&relative_pointer_manager->resources, wl_resource_get_link(manager_resource));

	wl_resource_set_implementation(manager_resource, &relative_pointer_manager_v1_impl,
		relative_pointer_manager, relative_pointer_manager_v1_handle_resource_destroy);

	wlr_log(WLR_DEBUG, "relative_pointer_v1 manager bound to client %p",
		wl_client);
}


/**
 * Implementations
 */

static const struct zwp_relative_pointer_manager_v1_interface relative_pointer_manager_v1_impl = {
	.destroy = relative_pointer_manager_v1_handle_destroy,
	.get_relative_pointer = relative_pointer_manager_v1_handle_get_relative_pointer,
};


static const struct zwp_relative_pointer_v1_interface relative_pointer_v1_impl = {
	.destroy = relative_pointer_v1_handle_destroy,
};


/**
 * Public functions
 */

struct wlr_relative_pointer_manager_v1 *wlr_relative_pointer_v1_create(struct wl_display *display) {
	struct wlr_relative_pointer_manager_v1 *relative_pointer_manager =
		calloc(1, sizeof(struct wlr_relative_pointer_manager_v1));

	if (relative_pointer_manager == NULL) {
		return NULL;
	}

	wl_list_init(&relative_pointer_manager->resources);

	wl_signal_init(&relative_pointer_manager->events.destroy);
	wl_signal_init(&relative_pointer_manager->events.new_relative_pointer);

	relative_pointer_manager->global = wl_global_create(display,
		&zwp_relative_pointer_manager_v1_interface, 1,
		relative_pointer_manager, relative_pointer_manager_v1_bind);

	if (relative_pointer_manager->global == NULL) {
		free(relative_pointer_manager);
		return NULL;
	}

	wlr_log(WLR_DEBUG, "relative_pointer_v1 manager created");

	return relative_pointer_manager;
}


void wlr_relative_pointer_v1_destroy(struct wlr_relative_pointer_manager_v1 *relative_pointer_manager) {
	if (relative_pointer_manager == NULL) {
		return;
	}

	wlr_signal_emit_safe(&relative_pointer_manager->events.destroy, relative_pointer_manager);

	struct wl_resource *resource;
	struct wl_resource *tmp_resource;
	wl_resource_for_each_safe(resource, tmp_resource, &relative_pointer_manager->resources) {
		wl_resource_destroy(resource);
	}

	wl_global_destroy(relative_pointer_manager->global);
	free(relative_pointer_manager);

	wlr_log(WLR_DEBUG, "relative_pointer_v1 manager destroyed");
}


void wlr_relative_pointer_v1_send_relative_motion(struct wl_resource *resource,
		uint64_t time, double dx, double dy,
		double dx_unaccel, double dy_unaccel) {
	assert(wl_resource_instance_of(resource, &zwp_relative_pointer_v1_interface,
		&relative_pointer_v1_impl));
	zwp_relative_pointer_v1_send_relative_motion(resource,
		(uint32_t)(time >> 32), (uint32_t)time,
		wl_fixed_from_double(dx), wl_fixed_from_double(dy),
		wl_fixed_from_double(dx_unaccel), wl_fixed_from_double(dy_unaccel));
}
