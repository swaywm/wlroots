#include <assert.h>
#include <stdlib.h>
#include <wayland-server.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_region.h>
#include <wlr/types/wlr_surface.h>
#include <wlr/util/log.h>
#include "util/signal.h"

#define COMPOSITOR_VERSION 4
#define SUBCOMPOSITOR_VERSION 1

static const char *subsurface_role = "wl_subsurface";

bool wlr_surface_is_subsurface(struct wlr_surface *surface) {
	return surface->role != NULL &&
		strcmp(surface->role, subsurface_role) == 0;
}

struct wlr_subsurface *wlr_subsurface_from_wlr_surface(
		struct wlr_surface *surface) {
	assert(wlr_surface_is_subsurface(surface));
	return (struct wlr_subsurface *)surface->role_data;
}

static const struct wl_subcompositor_interface subcompositor_impl;

static struct wlr_subcompositor *subcompositor_from_resource(
		struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource, &wl_subcompositor_interface,
		&subcompositor_impl));
	return wl_resource_get_user_data(resource);
}

static void subcompositor_handle_destroy(struct wl_client *client,
		struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static void subcompositor_handle_get_subsurface(struct wl_client *client,
		struct wl_resource *resource, uint32_t id,
		struct wl_resource *surface_resource,
		struct wl_resource *parent_resource) {
	struct wlr_subcompositor *subcompositor =
		subcompositor_from_resource(resource);
	struct wlr_surface *surface = wlr_surface_from_resource(surface_resource);
	struct wlr_surface *parent = wlr_surface_from_resource(parent_resource);

	static const char msg[] = "get_subsurface: wl_subsurface@";

	if (surface == parent) {
		wl_resource_post_error(resource,
			WL_SUBCOMPOSITOR_ERROR_BAD_SURFACE,
			"%s%d: wl_surface@%d cannot be its own parent",
			msg, id, wl_resource_get_id(surface_resource));
		return;
	}

	if (wlr_surface_is_subsurface(surface) &&
			wlr_subsurface_from_wlr_surface(surface) != NULL) {
		wl_resource_post_error(resource,
			WL_SUBCOMPOSITOR_ERROR_BAD_SURFACE,
			"%s%d: wl_surface@%d is already a sub-surface",
			msg, id, wl_resource_get_id(surface_resource));
		return;
	}

	if (wlr_surface_get_root_surface(parent) == surface) {
		wl_resource_post_error(resource,
			WL_SUBCOMPOSITOR_ERROR_BAD_SURFACE,
			"%s%d: wl_surface@%d is an ancestor of parent",
			msg, id, wl_resource_get_id(surface_resource));
		return;
	}

	if (wlr_surface_set_role(surface, subsurface_role, resource,
				WL_SUBCOMPOSITOR_ERROR_BAD_SURFACE) < 0) {
		return;
	}

	wlr_subsurface_create(surface, parent, wl_resource_get_version(resource),
		id, &subcompositor->subsurface_resources);
}

static const struct wl_subcompositor_interface subcompositor_impl = {
	.destroy = subcompositor_handle_destroy,
	.get_subsurface = subcompositor_handle_get_subsurface,
};

static void subcompositor_resource_destroy(struct wl_resource *resource) {
	wl_list_remove(wl_resource_get_link(resource));
}

static void subcompositor_bind(struct wl_client *client, void *data,
		uint32_t version, uint32_t id) {
	struct wlr_subcompositor *subcompositor = data;
	struct wl_resource *resource =
		wl_resource_create(client, &wl_subcompositor_interface, 1, id);
	if (resource == NULL) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(resource, &subcompositor_impl,
		subcompositor, subcompositor_resource_destroy);
	wl_list_insert(&subcompositor->resources, wl_resource_get_link(resource));
}

static void subcompositor_init(struct wlr_subcompositor *subcompositor,
		struct wl_display *display) {
	subcompositor->global = wl_global_create(display,
		&wl_subcompositor_interface, SUBCOMPOSITOR_VERSION, subcompositor,
		subcompositor_bind);
	if (subcompositor->global == NULL) {
		wlr_log_errno(L_ERROR, "Could not allocate subcompositor global");
		return;
	}
	wl_list_init(&subcompositor->resources);
	wl_list_init(&subcompositor->subsurface_resources);
}

static void subcompositor_finish(struct wlr_subcompositor *subcompositor) {
	wl_global_destroy(subcompositor->global);
	struct wl_resource *resource, *tmp;
	wl_resource_for_each_safe(resource, tmp,
			&subcompositor->subsurface_resources) {
		wl_resource_destroy(resource);
	}
	wl_resource_for_each_safe(resource, tmp, &subcompositor->resources) {
		wl_resource_destroy(resource);
	}
}


static const struct wl_compositor_interface compositor_impl;

static struct wlr_compositor *compositor_from_resource(
		struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource, &wl_compositor_interface,
		&compositor_impl));
	return wl_resource_get_user_data(resource);
}

static void compositor_create_surface(struct wl_client *client,
		struct wl_resource *resource, uint32_t id) {
	struct wlr_compositor *compositor = compositor_from_resource(resource);

	struct wlr_surface *surface = wlr_surface_create(client,
		wl_resource_get_version(resource), id, compositor->renderer,
		&compositor->surface_resources);
	if (surface == NULL) {
		return;
	}

	wlr_signal_emit_safe(&compositor->events.new_surface, surface);
}

static void compositor_create_region(struct wl_client *client,
		struct wl_resource *resource, uint32_t id) {
	struct wlr_compositor *compositor = compositor_from_resource(resource);

	wlr_region_create(client, 1, id, &compositor->region_resources);
}

static const struct wl_compositor_interface compositor_impl = {
	.create_surface = compositor_create_surface,
	.create_region = compositor_create_region,
};

static void compositor_resource_destroy(struct wl_resource *resource) {
	wl_list_remove(wl_resource_get_link(resource));
}

static void compositor_bind(struct wl_client *wl_client, void *data,
		uint32_t version, uint32_t id) {
	struct wlr_compositor *compositor = data;
	assert(wl_client && compositor);

	struct wl_resource *resource =
		wl_resource_create(wl_client, &wl_compositor_interface, version, id);
	if (resource == NULL) {
		wl_client_post_no_memory(wl_client);
		return;
	}
	wl_resource_set_implementation(resource, &compositor_impl,
		compositor, compositor_resource_destroy);
	wl_list_insert(&compositor->resources, wl_resource_get_link(resource));
}

void wlr_compositor_destroy(struct wlr_compositor *compositor) {
	if (compositor == NULL) {
		return;
	}
	wlr_signal_emit_safe(&compositor->events.destroy, compositor);
	subcompositor_finish(&compositor->subcompositor);
	wl_list_remove(&compositor->display_destroy.link);
	wl_global_destroy(compositor->global);
	struct wl_resource *resource, *tmp;
	wl_resource_for_each_safe(resource, tmp, &compositor->surface_resources) {
		wl_resource_destroy(resource);
	}
	wl_resource_for_each_safe(resource, tmp, &compositor->region_resources) {
		wl_resource_destroy(resource);
	}
	wl_resource_for_each_safe(resource, tmp, &compositor->resources) {
		wl_resource_destroy(resource);
	}
	free(compositor);
}

static void handle_display_destroy(struct wl_listener *listener, void *data) {
	struct wlr_compositor *compositor =
		wl_container_of(listener, compositor, display_destroy);
	wlr_compositor_destroy(compositor);
}

struct wlr_compositor *wlr_compositor_create(struct wl_display *display,
		struct wlr_renderer *renderer) {
	struct wlr_compositor *compositor =
		calloc(1, sizeof(struct wlr_compositor));
	if (!compositor) {
		wlr_log_errno(L_ERROR, "Could not allocate wlr compositor");
		return NULL;
	}

	compositor->global = wl_global_create(display, &wl_compositor_interface,
		COMPOSITOR_VERSION, compositor, compositor_bind);
	if (!compositor->global) {
		free(compositor);
		wlr_log_errno(L_ERROR, "Could not allocate compositor global");
		return NULL;
	}
	compositor->renderer = renderer;

	wl_list_init(&compositor->resources);
	wl_list_init(&compositor->surface_resources);
	wl_list_init(&compositor->region_resources);
	wl_signal_init(&compositor->events.new_surface);
	wl_signal_init(&compositor->events.destroy);

	subcompositor_init(&compositor->subcompositor, display);

	compositor->display_destroy.notify = handle_display_destroy;
	wl_display_add_destroy_listener(display, &compositor->display_destroy);

	return compositor;
}
