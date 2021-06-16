#include <assert.h>
#include <stdlib.h>
#include <wlr/types/wlr_surface.h>
#include <wlr/types/wlr_surface_suspension_v1.h>
#include <wlr/util/log.h>
#include "surface-suspension-v1-protocol.h"
#include "util/signal.h"

#define SURFACE_SUSPENSION_V1_VERSION 1

struct wlr_surface_suspension_v1 {
	struct wlr_surface *surface;
	struct wl_list link;
	struct wl_list resources;

	bool suspended;

	struct wl_listener surface_destroy;
};

static const struct wp_surface_suspension_manager_v1_interface manager_impl;

static struct wlr_surface_suspension_manager_v1 *manager_from_resource(
		struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource,
		&wp_surface_suspension_manager_v1_interface, &manager_impl));
	return wl_resource_get_user_data(resource);
}

static void surface_suspension_handle_destroy(struct wl_client *client,
		struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static const struct wp_surface_suspension_v1_interface surface_suspension_impl = {
	.destroy = surface_suspension_handle_destroy,
};

static void surface_handle_surface_destroy(struct wl_listener *listener,
		void *data) {
	struct wlr_surface_suspension_v1 *surface_suspension =
		wl_container_of(listener, surface_suspension, surface_destroy);

	wl_list_remove(&surface_suspension->surface_destroy.link);
	wl_list_remove(&surface_suspension->link);

	// Make all resources inert
	struct wl_resource *resource, *resource_tmp;
	wl_resource_for_each_safe(resource, resource_tmp, &surface_suspension->resources) {
		wl_resource_set_user_data(resource, NULL);
		wl_list_remove(wl_resource_get_link(resource));
		wl_list_init(wl_resource_get_link(resource));
	}

	free(surface_suspension);
}

static struct wlr_surface_suspension_v1 *
manager_get_or_create_surface_suspension(
		struct wlr_surface_suspension_manager_v1 *manager,
		struct wlr_surface *surface) {
	struct wlr_surface_suspension_v1 *surface_suspension;
	wl_list_for_each(surface_suspension, &manager->surfaces, link) {
		if (surface_suspension->surface == surface) {
			return surface_suspension;
		}
	}

	surface_suspension = calloc(1, sizeof(*surface_suspension));
	if (surface_suspension == NULL) {
		return NULL;
	}

	surface_suspension->surface = surface;
	wl_list_init(&surface_suspension->resources);

	surface_suspension->surface_destroy.notify = surface_handle_surface_destroy;
	wl_signal_add(&surface->events.destroy, &surface_suspension->surface_destroy);

	wl_list_insert(&manager->surfaces, &surface_suspension->link);

	return surface_suspension;
}

static void surface_handle_resource_destroy(struct wl_resource *resource) {
	wl_list_remove(wl_resource_get_link(resource));
}

static void manager_handle_get_surface_suspension(struct wl_client *client,
		struct wl_resource *manager_resource, uint32_t id,
		struct wl_resource *surface_resource) {
	struct wlr_surface_suspension_manager_v1 *manager =
		manager_from_resource(manager_resource);
	struct wlr_surface *surface = wlr_surface_from_resource(surface_resource);

	struct wlr_surface_suspension_v1 *surface_suspension =
		manager_get_or_create_surface_suspension(manager, surface);
	if (surface_suspension == NULL) {
		wl_client_post_no_memory(client);
		return;
	}

	uint32_t version = wl_resource_get_version(manager_resource);
	struct wl_resource *resource = wl_resource_create(client,
			&wp_surface_suspension_v1_interface, version, id);
	if (resource == NULL) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(resource, &surface_suspension_impl,
		surface_suspension, surface_handle_resource_destroy);

	wl_list_insert(&surface_suspension->resources, wl_resource_get_link(resource));

	if (surface_suspension->suspended) {
		wp_surface_suspension_v1_send_suspended(resource);
	}
}

static void manager_handle_destroy(struct wl_client *client,
		struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static const struct wp_surface_suspension_manager_v1_interface manager_impl = {
	.destroy = manager_handle_destroy,
	.get_surface_suspension = manager_handle_get_surface_suspension,
};

static void manager_bind(struct wl_client *client, void *data,
		uint32_t version, uint32_t id) {
	struct wlr_surface_suspension_manager_v1 *manager = data;

	struct wl_resource *resource = wl_resource_create(client,
		&wp_surface_suspension_manager_v1_interface, version, id);
	if (resource == NULL) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(resource, &manager_impl, manager, NULL);
}

static void handle_display_destroy(struct wl_listener *listener, void *data) {
	struct wlr_surface_suspension_manager_v1 *manager =
		wl_container_of(listener, manager, display_destroy);

	wlr_signal_emit_safe(&manager->events.destroy, NULL);

	wl_list_remove(&manager->display_destroy.link);
	wl_global_destroy(manager->global);
	free(manager);
}

struct wlr_surface_suspension_manager_v1 *
wlr_surface_suspension_manager_v1_create(struct wl_display *display) {
	struct wlr_surface_suspension_manager_v1 *manager =
		calloc(1, sizeof(*manager));
	if (manager == NULL) {
		return NULL;
	}

	wl_signal_init(&manager->events.destroy);
	wl_list_init(&manager->surfaces);

	manager->global = wl_global_create(display,
		&wp_surface_suspension_manager_v1_interface,
		SURFACE_SUSPENSION_V1_VERSION, manager, manager_bind);
	if (manager->global == NULL) {
		free(manager);
		return NULL;
	}

	manager->display_destroy.notify = handle_display_destroy;
	wl_display_add_destroy_listener(display, &manager->display_destroy);

	return manager;
}

void wlr_surface_suspension_manager_v1_set_suspended(
		struct wlr_surface_suspension_manager_v1 *manager,
		struct wlr_surface *surface, bool suspended) {
	struct wlr_surface_suspension_v1 *surface_suspension =
		manager_get_or_create_surface_suspension(manager, surface);
	if (surface_suspension == NULL) {
		wlr_log(WLR_ERROR, "Allocation failed");
		return;
	}

	if (surface_suspension->suspended == suspended) {
		return;
	}

	surface_suspension->suspended = suspended;

	struct wl_resource *resource;
	wl_resource_for_each(resource, &surface_suspension->resources) {
		if (suspended) {
			wp_surface_suspension_v1_send_suspended(resource);
		} else {
			wp_surface_suspension_v1_send_resumed(resource);
		}
	}
}
