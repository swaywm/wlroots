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

#define RELATIVE_POINTER_MANAGER_VERSION 1

static const struct zwp_relative_pointer_manager_v1_interface relative_pointer_manager_v1_impl;
static const struct zwp_relative_pointer_v1_interface relative_pointer_v1_impl;


/**
 * helper functions
 */

struct wlr_relative_pointer_v1 *wlr_relative_pointer_v1_from_resource(struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource, &zwp_relative_pointer_v1_interface,
		&relative_pointer_v1_impl));
	return wl_resource_get_user_data(resource);
}


static struct wlr_relative_pointer_manager_v1 *relative_pointer_manager_from_resource(struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource, &zwp_relative_pointer_manager_v1_interface,
		&relative_pointer_manager_v1_impl));
	return wl_resource_get_user_data(resource);
}


/**
 * relative_pointer handler functions
 */

static void relative_pointer_destroy(struct wlr_relative_pointer_v1 *relative_pointer) {
	wlr_signal_emit_safe(&relative_pointer->events.destroy, relative_pointer);

	wl_list_remove(&relative_pointer->link);
	wl_list_remove(&relative_pointer->seat_destroy.link);
	free(relative_pointer);
}

static void relative_pointer_v1_handle_resource_destroy(struct wl_resource *resource) {
	struct wlr_relative_pointer_v1 *relative_pointer =
		wlr_relative_pointer_v1_from_resource(resource);
	if (relative_pointer == NULL) {
		return;
	}
	relative_pointer_destroy(relative_pointer);
}


static void relative_pointer_v1_handle_destroy(struct wl_client *client,
		struct wl_resource *resource) {
	struct wlr_relative_pointer_v1 *relative_pointer =
		wlr_relative_pointer_v1_from_resource(resource);
	wlr_log(WLR_DEBUG, "relative_pointer_v1 %p released by client %p",
		relative_pointer, client);

	wl_resource_destroy(resource);
}

static void relative_pointer_handle_seat_destroy(struct wl_listener *listener,
		void *data) {
	struct wlr_relative_pointer_v1 *relative_pointer =
		wl_container_of(listener, relative_pointer, seat_destroy);

	relative_pointer_destroy(relative_pointer);
	wl_resource_set_user_data(relative_pointer->resource, NULL);
}

/**
 * relative_pointer_manager handler functions
 */

static void relative_pointer_manager_v1_handle_resource_destroy(struct wl_resource *resource) {
	wl_list_remove(wl_resource_get_link(resource));
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

	relative_pointer->resource = relative_pointer_resource;
	relative_pointer->seat = seat_client->seat;

	wl_signal_init(&relative_pointer->events.destroy);

	wl_resource_set_implementation(relative_pointer_resource, &relative_pointer_v1_impl,
		relative_pointer, relative_pointer_v1_handle_resource_destroy);

	struct wlr_relative_pointer_manager_v1 *relative_pointer_manager =
		relative_pointer_manager_from_resource(resource);

	wl_list_insert(&relative_pointer_manager->relative_pointers,
			&relative_pointer->link);

	wl_signal_add(&relative_pointer->seat->events.destroy,
			&relative_pointer->seat_destroy);
	relative_pointer->seat_destroy.notify = relative_pointer_handle_seat_destroy;

	wlr_signal_emit_safe(&relative_pointer_manager->events.new_relative_pointer,
		relative_pointer);

	wlr_log(WLR_DEBUG, "relative_pointer_v1 %p created for client %p",
		relative_pointer, client);
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

static void handle_display_destroy(struct wl_listener *listener, void *data) {
	struct wlr_relative_pointer_manager_v1 *manager =
		wl_container_of(listener, manager, display_destroy_listener);
	wlr_relative_pointer_manager_v1_destroy(manager);
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

struct wlr_relative_pointer_manager_v1 *wlr_relative_pointer_manager_v1_create(struct wl_display *display) {
	struct wlr_relative_pointer_manager_v1 *relative_pointer_manager =
		calloc(1, sizeof(struct wlr_relative_pointer_manager_v1));

	if (relative_pointer_manager == NULL) {
		return NULL;
	}

	wl_list_init(&relative_pointer_manager->resources);
	wl_list_init(&relative_pointer_manager->relative_pointers);

	relative_pointer_manager->global = wl_global_create(display,
		&zwp_relative_pointer_manager_v1_interface, RELATIVE_POINTER_MANAGER_VERSION,
		relative_pointer_manager, relative_pointer_manager_v1_bind);

	if (relative_pointer_manager->global == NULL) {
		free(relative_pointer_manager);
		return NULL;
	}

	wl_signal_init(&relative_pointer_manager->events.destroy);
	wl_signal_init(&relative_pointer_manager->events.new_relative_pointer);

	relative_pointer_manager->display_destroy_listener.notify = handle_display_destroy;
	wl_display_add_destroy_listener(display, &relative_pointer_manager->display_destroy_listener);

	wlr_log(WLR_DEBUG, "relative_pointer_v1 manager created");

	return relative_pointer_manager;
}


void wlr_relative_pointer_manager_v1_destroy(struct wlr_relative_pointer_manager_v1 *relative_pointer_manager) {
	if (relative_pointer_manager == NULL) {
		return;
	}

	wlr_signal_emit_safe(&relative_pointer_manager->events.destroy, relative_pointer_manager);
	wl_list_remove(&relative_pointer_manager->display_destroy_listener.link);

	struct wl_resource *resource;
	struct wl_resource *tmp_resource;
	wl_resource_for_each_safe(resource, tmp_resource, &relative_pointer_manager->resources) {
		wl_resource_destroy(resource);
	}

	wl_global_destroy(relative_pointer_manager->global);
	free(relative_pointer_manager);

	wlr_log(WLR_DEBUG, "relative_pointer_v1 manager destroyed");
}


void wlr_relative_pointer_v1_send_relative_motion(struct wlr_relative_pointer_v1 *relative_pointer,
		uint64_t time, double dx, double dy,
		double dx_unaccel, double dy_unaccel) {
	struct wlr_seat_client *client =
		relative_pointer->seat->pointer_state.focused_client;

	if (client == NULL) {
		return;
	}
	zwp_relative_pointer_v1_send_relative_motion(relative_pointer->resource,
		(uint32_t)(time >> 32), (uint32_t)time,
		wl_fixed_from_double(dx), wl_fixed_from_double(dy),
		wl_fixed_from_double(dx_unaccel), wl_fixed_from_double(dy_unaccel));

	struct wl_resource *resource;
	wl_resource_for_each(resource, &client->pointers) {
		if (wlr_seat_client_from_pointer_resource(resource) == NULL) {
			continue;
		}
		wl_pointer_send_frame(resource);
	}
}
