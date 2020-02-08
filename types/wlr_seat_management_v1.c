#include <assert.h>
#include <stdlib.h>
#include <wayland-util.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_seat_management_v1.h>
#include "wlr-seat-management-unstable-v1-protocol.h"

static const struct zwlr_seat_manager_v1_interface manager_impl;
static const struct zwlr_chair_v1_interface chair_impl;

void chair_handle_destroy(struct wl_client *client,
		struct wl_resource *resource);

static struct wlr_seat_manager_v1 *manager_from_resource(
		struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource,
		&zwlr_seat_manager_v1_interface, &manager_impl));
	return wl_resource_get_user_data(resource);
}

static struct wlr_chair_v1 *chair_from_resource(
		struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource, &zwlr_chair_v1_interface,
		&chair_impl));
	return wl_resource_get_user_data(resource);
}

static void manager_handle_destroy(struct wl_client *client,
		struct wl_resource *manager_resource) {
	wl_resource_destroy(manager_resource);
}

static void chair_destroy(struct wlr_chair_v1 *chair) {
	wl_list_remove(&chair->link);
	chair->manager->destroy_seat(chair->seat);
	wl_resource_set_user_data(chair->resource, NULL);
	free(chair);
}

static void chair_destroy_resource(struct wl_resource *resource) {
	struct wlr_chair_v1 *chair = chair_from_resource(resource);
	if (chair) {
		chair_destroy(chair);
	}
}

void chair_handle_destroy(struct wl_client *client,
		struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static const struct zwlr_chair_v1_interface chair_impl = {
	.destroy = chair_handle_destroy,
};

static void manager_create_chair(struct wl_client *client,
		struct wl_resource *manager_resource, const char *name,
		uint32_t id) {
	struct wlr_seat_manager_v1 *manager =
		manager_from_resource(manager_resource);

	struct wlr_chair_v1 *chair = calloc(1, sizeof(struct wlr_chair_v1));
	if (!chair) {
		wl_client_post_no_memory(client);
		return;
	}

	chair->manager = manager;

	chair->seat = manager->create_seat(name);
	if (!chair->seat) {
		goto failure;
	}

	chair->resource = wl_resource_create(client, &zwlr_chair_v1_interface,
		wl_resource_get_version(manager_resource), id);
	if (!chair->resource) {
		goto resource_failure;
	}

	wl_resource_set_implementation(chair->resource, &chair_impl,
		chair, chair_destroy_resource);

	wl_list_insert(&manager->chairs, &chair->link);

	return;

resource_failure:
	manager->destroy_seat(chair->seat);
failure:
	free(chair);
}

static const struct zwlr_seat_manager_v1_interface manager_impl = {
	.create_chair = manager_create_chair,
	.destroy = manager_handle_destroy,
};

static void handle_display_destroy(struct wl_listener *listener, void *data) {
	struct wlr_seat_manager_v1 *manager =
		wl_container_of(listener, manager, display_destroy);

	struct wlr_chair_v1 *chair, *tmp_chair;
	wl_list_for_each_safe(chair, tmp_chair, &manager->chairs, link) {
		chair_destroy(chair);
	}

	wl_list_remove(&manager->display_destroy.link);
	wl_global_destroy(manager->global);
	free(manager);
}

static void seat_manager_bind(struct wl_client *client, void *data,
		uint32_t version, uint32_t id) {
	struct wlr_virtual_pointer_manager_v1 *manager = data;

	struct wl_resource *resource = wl_resource_create(client,
		&zwlr_seat_manager_v1_interface, version, id);

	if (!resource) {
		wl_client_post_no_memory(client);
		return;
	}

	wl_resource_set_implementation(resource, &manager_impl, manager, NULL);
}

struct wlr_seat_manager_v1 *wlr_seat_manager_v1_create(
		struct wl_display *display) {
	struct wlr_seat_manager_v1 *manager =
		calloc(1, sizeof(struct wlr_seat_manager_v1));
	if (!manager) {
		return NULL;
	}

	wl_list_init(&manager->chairs);

	manager->global = wl_global_create(display,
		&zwlr_seat_manager_v1_interface, 1, manager, seat_manager_bind);
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
