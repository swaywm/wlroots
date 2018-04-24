#include <assert.h>
#include <stdlib.h>
#include <wayland-server.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_region.h>
#include <wlr/types/wlr_surface.h>
#include <wlr/util/log.h>
#include "util/signal.h"

static const char *subsurface_role = "wl_subsurface";

bool wlr_surface_is_subsurface(struct wlr_surface *surface) {
	return surface->role != NULL &&
		strcmp(surface->role, subsurface_role) == 0;
}

struct wlr_subsurface *wlr_subsurface_from_surface(
		struct wlr_surface *surface) {
	assert(wlr_surface_is_subsurface(surface));
	return (struct wlr_subsurface *)surface->role_data;
}

static void subcompositor_handle_destroy(struct wl_client *client,
		struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static void subcompositor_handle_get_subsurface(struct wl_client *client,
		struct wl_resource *resource, uint32_t id,
		struct wl_resource *surface_resource,
		struct wl_resource *parent_resource) {
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
			wlr_subsurface_from_surface(surface) != NULL) {
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

	wlr_surface_make_subsurface(surface, parent, id);
}

static const struct wl_subcompositor_interface subcompositor_interface = {
	.destroy = subcompositor_handle_destroy,
	.get_subsurface = subcompositor_handle_get_subsurface,
};

static void subcompositor_bind(struct wl_client *client, void *data,
		uint32_t version, uint32_t id) {
	struct wlr_subcompositor *subcompositor = data;
	struct wl_resource *resource =
		wl_resource_create(client, &wl_subcompositor_interface, 1, id);
	if (resource == NULL) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(resource, &subcompositor_interface,
		subcompositor, NULL);
	wl_list_insert(&subcompositor->wl_resources, wl_resource_get_link(resource));
}

static void subcompositor_init(struct wlr_subcompositor *subcompositor,
		struct wl_display *display) {
	subcompositor->wl_global = wl_global_create(display,
		&wl_subcompositor_interface, 1, subcompositor, subcompositor_bind);
	if (subcompositor->wl_global == NULL) {
		wlr_log_errno(L_ERROR, "Could not allocate subcompositor global");
		return;
	}
	wl_list_init(&subcompositor->wl_resources);
}

static void subcompositor_finish(struct wlr_subcompositor *subcompositor) {
	wl_global_destroy(subcompositor->wl_global);
}


static const struct wl_compositor_interface wl_compositor_impl;

static struct wlr_compositor *compositor_from_resource(
		struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource, &wl_compositor_interface,
		&wl_compositor_impl));
	return wl_resource_get_user_data(resource);
}

static void destroy_surface_listener(struct wl_listener *listener, void *data) {
	wl_list_remove(wl_resource_get_link(data));
}

static void wl_compositor_create_surface(struct wl_client *client,
		struct wl_resource *resource, uint32_t id) {
	struct wlr_compositor *compositor = compositor_from_resource(resource);

	struct wl_resource *surface_resource = wl_resource_create(client,
		&wl_surface_interface, wl_resource_get_version(resource), id);
	if (surface_resource == NULL) {
		wl_resource_post_no_memory(resource);
		return;
	}

	struct wlr_surface *surface = wlr_surface_create(surface_resource,
		compositor->renderer);
	if (surface == NULL) {
		wl_resource_destroy(surface_resource);
		wl_resource_post_no_memory(resource);
		return;
	}
	surface->compositor_data = compositor;
	surface->compositor_listener.notify = &destroy_surface_listener;
	wl_resource_add_destroy_listener(surface_resource,
		&surface->compositor_listener);

	wl_list_insert(&compositor->surfaces,
		wl_resource_get_link(surface_resource));
	wlr_signal_emit_safe(&compositor->events.new_surface, surface);
}

static void wl_compositor_create_region(struct wl_client *client,
		struct wl_resource *resource, uint32_t id) {
	wlr_region_create(client, resource, id);
}

static const struct wl_compositor_interface wl_compositor_impl = {
	.create_surface = wl_compositor_create_surface,
	.create_region = wl_compositor_create_region
};

static void wl_compositor_destroy(struct wl_resource *resource) {
	struct wlr_compositor *compositor = compositor_from_resource(resource);
	struct wl_resource *_resource = NULL;
	wl_resource_for_each(_resource, &compositor->wl_resources) {
		if (_resource == resource) {
			struct wl_list *link = wl_resource_get_link(_resource);
			wl_list_remove(link);
			break;
		}
	}
}

static void wl_compositor_bind(struct wl_client *wl_client, void *data,
		uint32_t version, uint32_t id) {
	struct wlr_compositor *compositor = data;
	assert(wl_client && compositor);

	struct wl_resource *resource =
		wl_resource_create(wl_client, &wl_compositor_interface, version, id);
	if (resource == NULL) {
		wl_client_post_no_memory(wl_client);
		return;
	}
	wl_resource_set_implementation(resource, &wl_compositor_impl,
		compositor, wl_compositor_destroy);
	wl_list_insert(&compositor->wl_resources, wl_resource_get_link(resource));
}

void wlr_compositor_destroy(struct wlr_compositor *compositor) {
	if (compositor == NULL) {
		return;
	}
	wlr_signal_emit_safe(&compositor->events.destroy, compositor);
	subcompositor_finish(&compositor->subcompositor);
	wl_list_remove(&compositor->display_destroy.link);
	wl_global_destroy(compositor->wl_global);
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

	struct wl_global *compositor_global = wl_global_create(display,
		&wl_compositor_interface, 4, compositor, wl_compositor_bind);
	if (!compositor_global) {
		wlr_log_errno(L_ERROR, "Could not allocate compositor global");
		free(compositor);
		return NULL;
	}
	compositor->wl_global = compositor_global;
	compositor->renderer = renderer;

	wl_list_init(&compositor->wl_resources);
	wl_list_init(&compositor->surfaces);
	wl_signal_init(&compositor->events.new_surface);
	wl_signal_init(&compositor->events.destroy);

	subcompositor_init(&compositor->subcompositor, display);

	compositor->display_destroy.notify = handle_display_destroy;
	wl_display_add_destroy_listener(display, &compositor->display_destroy);

	return compositor;
}
