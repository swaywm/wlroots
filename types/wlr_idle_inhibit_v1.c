#include <assert.h>
#include <stdlib.h>
#include <wlr/util/log.h>
#include <util/signal.h>
#include <wlr/types/wlr_surface.h>
#include <wlr/types/wlr_idle_inhibit_v1.h>
#include "wayland-util.h"
#include "wayland-server.h"
#include "idle-inhibit-unstable-v1-protocol.h"

static const struct zwp_idle_inhibit_manager_v1_interface idle_inhibit_impl;

static const struct zwp_idle_inhibitor_v1_interface idle_inhibitor_impl;

static struct wlr_idle_inhibit_manager_v1 *
wlr_idle_inhibit_manager_v1_from_resource(struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource, &zwp_idle_inhibit_manager_v1_interface,
		&idle_inhibit_impl));
	return wl_resource_get_user_data(resource);
}

static struct wlr_idle_inhibitor_v1 *
wlr_idle_inhibitor_v1_from_resource(struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource, &zwp_idle_inhibitor_v1_interface,
		&idle_inhibitor_impl));
	return wl_resource_get_user_data(resource);
}

static void idle_inhibitor_v1_destroy(struct wlr_idle_inhibitor_v1 *inhibitor) {
	if (!inhibitor) {
		return;
	}

	wlr_signal_emit_safe(&inhibitor->events.destroy, inhibitor->surface);

	wl_resource_set_user_data(inhibitor->resource, NULL);
	wl_list_remove(&inhibitor->link);
	wl_list_remove(&inhibitor->surface_destroy.link);
	free(inhibitor);
}

static void idle_inhibitor_v1_handle_resource_destroy(struct wl_resource *resource) {
	struct wlr_idle_inhibitor_v1 *inhibitor =
		wlr_idle_inhibitor_v1_from_resource(resource);
	idle_inhibitor_v1_destroy(inhibitor);
}

static void idle_inhibitor_handle_surface_destroy(
		struct wl_listener *listener, void *data) {
	struct wlr_idle_inhibitor_v1 *inhibitor =
		wl_container_of(listener, inhibitor, surface_destroy);
	idle_inhibitor_v1_destroy(inhibitor);
}

static void idle_inhibitor_v1_handle_destroy(struct wl_client *client,
		struct wl_resource *manager_resource) {
	wl_resource_destroy(manager_resource);
}

static const struct zwp_idle_inhibitor_v1_interface idle_inhibitor_impl = {
	.destroy = idle_inhibitor_v1_handle_destroy,
};

static void manager_handle_create_inhibitor(struct wl_client *client,
		struct wl_resource *resource, uint32_t id,
		struct wl_resource *surface_resource) {
	struct wlr_surface *surface = wlr_surface_from_resource(surface_resource);
	struct wlr_idle_inhibit_manager_v1 *manager =
		wlr_idle_inhibit_manager_v1_from_resource(resource);

	struct wlr_idle_inhibitor_v1 *inhibitor =
		calloc(1, sizeof(struct wlr_idle_inhibitor_v1));
	if (!inhibitor) {
		wl_client_post_no_memory(client);
		return;
	}

	struct wl_resource *wl_resource = wl_resource_create(client,
		&zwp_idle_inhibitor_v1_interface, 1, id);
	if (!wl_resource) {
		wl_client_post_no_memory(client);
		free(inhibitor);
		return;
	}

	inhibitor->resource = wl_resource;
	inhibitor->surface = surface;
	wl_signal_init(&inhibitor->events.destroy);

	inhibitor->surface_destroy.notify = idle_inhibitor_handle_surface_destroy;
	wl_signal_add(&surface->events.destroy, &inhibitor->surface_destroy);


	wl_resource_set_implementation(wl_resource, &idle_inhibitor_impl,
		inhibitor, idle_inhibitor_v1_handle_resource_destroy);

	wl_list_insert(&manager->inhibitors, &inhibitor->link);
	wlr_signal_emit_safe(&manager->events.new_inhibitor, inhibitor);
}

static void idle_inhibit_manager_v1_handle_resource_destroy(
		struct wl_resource *resource) {
	wl_list_remove(wl_resource_get_link(resource));
}

static void manager_handle_destroy(struct wl_client *client,
		struct wl_resource *manager_resource) {
	wl_resource_destroy(manager_resource);
}

static const struct zwp_idle_inhibit_manager_v1_interface idle_inhibit_impl = {
	.destroy = manager_handle_destroy,
	.create_inhibitor = manager_handle_create_inhibitor,
};

static void handle_display_destroy(struct wl_listener *listener, void *data) {
	struct wlr_idle_inhibit_manager_v1 *idle_inhibit =
		wl_container_of(listener, idle_inhibit, display_destroy);

	wlr_idle_inhibit_v1_destroy(idle_inhibit);
}

static void idle_inhibit_bind(struct wl_client *wl_client, void *data,
		uint32_t version, uint32_t id) {
	struct wlr_idle_inhibit_manager_v1 *idle_inhibit = data;

	struct wl_resource *wl_resource  = wl_resource_create(wl_client,
		&zwp_idle_inhibit_manager_v1_interface, version, id);

	if (!wl_resource) {
		wl_client_post_no_memory(wl_client);
		return;
	}

	wl_list_insert(&idle_inhibit->resources, wl_resource_get_link(wl_resource));

	wl_resource_set_implementation(wl_resource, &idle_inhibit_impl,
		idle_inhibit, idle_inhibit_manager_v1_handle_resource_destroy);
	wlr_log(WLR_DEBUG, "idle_inhibit bound");
}

void wlr_idle_inhibit_v1_destroy(struct wlr_idle_inhibit_manager_v1 *idle_inhibit) {
	if (!idle_inhibit) {
		return;
	}

	wl_list_remove(&idle_inhibit->display_destroy.link);

	struct wlr_idle_inhibitor_v1 *inhibitor;
	struct wlr_idle_inhibitor_v1 *tmp;
	wl_list_for_each_safe(inhibitor, tmp, &idle_inhibit->inhibitors, link) {
		idle_inhibitor_v1_destroy(inhibitor);
	}

	struct wl_resource *resource;
	struct wl_resource *tmp_resource;
	wl_resource_for_each_safe(resource, tmp_resource, &idle_inhibit->resources) {
		wl_resource_destroy(resource);
	}

	wl_global_destroy(idle_inhibit->global);
	free(idle_inhibit);
}

struct wlr_idle_inhibit_manager_v1 *wlr_idle_inhibit_v1_create(struct wl_display *display) {
	struct wlr_idle_inhibit_manager_v1 *idle_inhibit =
		calloc(1, sizeof(struct wlr_idle_inhibit_manager_v1));

	if (!idle_inhibit) {
		return NULL;
	}

	wl_list_init(&idle_inhibit->resources);
	wl_list_init(&idle_inhibit->inhibitors);
	idle_inhibit->display_destroy.notify = handle_display_destroy;
	wl_display_add_destroy_listener(display, &idle_inhibit->display_destroy);
	wl_signal_init(&idle_inhibit->events.new_inhibitor);

	idle_inhibit->global = wl_global_create(display,
		&zwp_idle_inhibit_manager_v1_interface, 1,
		idle_inhibit, idle_inhibit_bind);

	if (!idle_inhibit->global) {
		wl_list_remove(&idle_inhibit->display_destroy.link);
		free(idle_inhibit);
		return NULL;
	}

	wlr_log(WLR_DEBUG, "idle_inhibit manager created");

	return idle_inhibit;
}
