#include <assert.h>
#include <stdlib.h>
#include <wayland-util.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_transient_seat_v1.h>
#include "transient-seat-unstable-v1-protocol.h"

static const struct zext_transient_seat_manager_v1_interface manager_impl;
static const struct zext_transient_seat_v1_interface transient_seat_impl;

void transient_seat_handle_destroy(struct wl_client *client,
		struct wl_resource *resource);

static struct wlr_transient_seat_manager_v1 *manager_from_resource(
		struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource,
		&zext_transient_seat_manager_v1_interface, &manager_impl));
	return wl_resource_get_user_data(resource);
}

static struct wlr_transient_seat_v1 *transient_seat_from_resource(
		struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource,
		&zext_transient_seat_v1_interface, &transient_seat_impl));
	return wl_resource_get_user_data(resource);
}

static void manager_handle_destroy(struct wl_client *client,
		struct wl_resource *manager_resource) {
	wl_resource_destroy(manager_resource);
}

static void transient_seat_destroy(struct wlr_transient_seat_v1 *transient_seat) {
	wl_list_remove(&transient_seat->link);
	if (transient_seat->seat) {
		transient_seat->manager->destroy_seat(transient_seat->seat);
	}
	wl_resource_set_user_data(transient_seat->resource, NULL);
	free(transient_seat);
}

static void transient_seat_destroy_resource(struct wl_resource *resource) {
	struct wlr_transient_seat_v1 *transient_seat =
		transient_seat_from_resource(resource);
	if (transient_seat) {
		transient_seat_destroy(transient_seat);
	}
}

void transient_seat_handle_destroy(struct wl_client *client,
		struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static const struct zext_transient_seat_v1_interface transient_seat_impl = {
	.destroy = transient_seat_handle_destroy,
};

static void manager_create_transient_seat(struct wl_client *client,
		struct wl_resource *manager_resource,
		const char *suggested_name, uint32_t id) {
	struct wlr_transient_seat_manager_v1 *manager =
		manager_from_resource(manager_resource);

	struct wlr_transient_seat_v1 *transient_seat =
		calloc(1, sizeof(struct wlr_transient_seat_v1));
	if (!transient_seat) {
		wl_client_post_no_memory(client);
		return;
	}

	transient_seat->manager = manager;

	transient_seat->resource = wl_resource_create(client,
		&zext_transient_seat_v1_interface,
		wl_resource_get_version(manager_resource), id);
	if (!transient_seat->resource) {
		wl_client_post_no_memory(client);
		free(transient_seat);
		return;
	}

	wl_resource_set_implementation(transient_seat->resource,
		&transient_seat_impl, transient_seat,
		transient_seat_destroy_resource);

	wl_list_insert(&manager->transient_seats, &transient_seat->link);

	if (manager->find_seat(suggested_name)) {
		zext_transient_seat_v1_send_failed(transient_seat->resource,
				ZEXT_TRANSIENT_SEAT_V1_ERROR_NAME_TAKEN);
		transient_seat_destroy(transient_seat);
		return;
	}

	transient_seat->seat = manager->create_seat(suggested_name);
	if (transient_seat->seat) {
		zext_transient_seat_v1_send_ready(transient_seat->resource,
				suggested_name);
	} else {
		zext_transient_seat_v1_send_failed(transient_seat->resource,
				ZEXT_TRANSIENT_SEAT_V1_ERROR_UNSPEC);
		transient_seat_destroy(transient_seat);
	}
}

static const struct zext_transient_seat_manager_v1_interface manager_impl = {
	.create = manager_create_transient_seat,
	.destroy = manager_handle_destroy,
};

static void handle_display_destroy(struct wl_listener *listener, void *data) {
	struct wlr_transient_seat_manager_v1 *manager =
		wl_container_of(listener, manager, display_destroy);

	struct wlr_transient_seat_v1 *transient_seat, *tmp_transient_seat;
	wl_list_for_each_safe(transient_seat, tmp_transient_seat,
			&manager->transient_seats, link) {
		transient_seat_destroy(transient_seat);
	}

	wl_list_remove(&manager->display_destroy.link);
	wl_global_destroy(manager->global);
	free(manager);
}

static void transient_seat_manager_bind(struct wl_client *client, void *data,
		uint32_t version, uint32_t id) {
	struct wlr_virtual_pointer_manager_v1 *manager = data;

	struct wl_resource *resource = wl_resource_create(client,
		&zext_transient_seat_manager_v1_interface, version, id);

	if (!resource) {
		wl_client_post_no_memory(client);
		return;
	}

	wl_resource_set_implementation(resource, &manager_impl, manager, NULL);
}

struct wlr_transient_seat_manager_v1 *wlr_transient_seat_manager_v1_create(
		struct wl_display *display) {
	struct wlr_transient_seat_manager_v1 *manager =
		calloc(1, sizeof(struct wlr_transient_seat_manager_v1));
	if (!manager) {
		return NULL;
	}

	wl_list_init(&manager->transient_seats);

	manager->global = wl_global_create(display,
		&zext_transient_seat_manager_v1_interface, 1, manager,
		transient_seat_manager_bind);
	if (!manager->global) {
		goto failure;
	}

	manager->display_destroy.notify = handle_display_destroy;
	wl_display_add_destroy_listener(display, &manager->display_destroy);

	return manager;

failure:
	free(manager);
	return NULL;
}
