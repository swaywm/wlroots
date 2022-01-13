#include <assert.h>
#include <stdlib.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_surface.h>
#include "types/wlr_region.h"
#include "types/wlr_surface.h"
#include "util/signal.h"

#define SUBCOMPOSITOR_VERSION 1

extern const struct wlr_surface_role subsurface_role;

bool wlr_surface_is_subsurface(struct wlr_surface *surface) {
	return surface->role == &subsurface_role;
}

struct wlr_subsurface *wlr_subsurface_from_wlr_surface(
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
			"%s%" PRIu32 ": wl_surface@%" PRIu32 " cannot be its own parent",
			msg, id, wl_resource_get_id(surface_resource));
		return;
	}

	if (wlr_surface_is_subsurface(surface) &&
			wlr_subsurface_from_wlr_surface(surface) != NULL) {
		wl_resource_post_error(resource,
			WL_SUBCOMPOSITOR_ERROR_BAD_SURFACE,
			"%s%" PRIu32 ": wl_surface@%" PRIu32 " is already a sub-surface",
			msg, id, wl_resource_get_id(surface_resource));
		return;
	}

	if (wlr_surface_get_root_surface(parent) == surface) {
		wl_resource_post_error(resource,
			WL_SUBCOMPOSITOR_ERROR_BAD_SURFACE,
			"%s%" PRIu32 ": wl_surface@%" PRIu32 " is an ancestor of parent",
			msg, id, wl_resource_get_id(surface_resource));
		return;
	}

	if (!wlr_surface_set_role(surface, &subsurface_role, NULL,
			resource, WL_SUBCOMPOSITOR_ERROR_BAD_SURFACE)) {
		return;
	}

	subsurface_create(surface, parent, wl_resource_get_version(resource), id);
}

static const struct wl_subcompositor_interface subcompositor_impl = {
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
	wl_resource_set_implementation(resource, &subcompositor_impl,
		subcompositor, NULL);
}

static void subcompositor_handle_display_destroy(
		struct wl_listener *listener, void *data) {
	struct wlr_subcompositor *subcompositor =
		wl_container_of(listener, subcompositor, display_destroy);
	wlr_signal_emit_safe(&subcompositor->events.destroy, NULL);
	wl_list_remove(&subcompositor->display_destroy.link);
	wl_global_destroy(subcompositor->global);
	free(subcompositor);
}

struct wlr_subcompositor *wlr_subcompositor_create(struct wl_display *display) {
	struct wlr_subcompositor *subcompositor =
		calloc(1, sizeof(*subcompositor));
	if (!subcompositor) {
		return NULL;
	}

	subcompositor->global = wl_global_create(display,
		&wl_subcompositor_interface, SUBCOMPOSITOR_VERSION,
		subcompositor, subcompositor_bind);
	if (!subcompositor->global) {
		free(subcompositor);
		return NULL;
	}

	wl_signal_init(&subcompositor->events.destroy);

	subcompositor->display_destroy.notify = subcompositor_handle_display_destroy;
	wl_display_add_destroy_listener(display, &subcompositor->display_destroy);

	return subcompositor;
}
