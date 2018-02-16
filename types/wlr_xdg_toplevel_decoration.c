#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <wlr/types/wlr_xdg_toplevel_decoration.h>
#include <wlr/util/log.h>
#include "util/signal.h"
#include "xdg-toplevel-decoration-unstable-v1-protocol.h"

static void decoration_handle_destroy(struct wl_client *client,
		struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static void decoration_handle_set_mode(struct wl_client *client,
		struct wl_resource *resource,
		enum zxdg_toplevel_decoration_v1_mode mode) {
	struct wlr_xdg_toplevel_decoration *decoration =
		wl_resource_get_user_data(resource);

	decoration->next_mode = (enum wlr_xdg_toplevel_decoration_mode)mode;
	wlr_signal_emit_safe(&decoration->events.request_mode, decoration);
}

static const struct zxdg_toplevel_decoration_v1_interface decoration_impl = {
	.destroy = decoration_handle_destroy,
	.set_mode = decoration_handle_set_mode,
};

void wlr_xdg_toplevel_decoration_send_preferred_mode(
		struct wlr_xdg_toplevel_decoration *decoration,
		enum wlr_xdg_toplevel_decoration_mode mode) {
	zxdg_toplevel_decoration_v1_send_preferred_mode(decoration->resource, mode);
}

uint32_t wlr_xdg_toplevel_decoration_set_mode(
		struct wlr_xdg_toplevel_decoration *decoration,
		enum wlr_xdg_toplevel_decoration_mode mode) {
	decoration->pending_mode = mode;
	return wlr_xdg_surface_schedule_configure(decoration->surface);
}

static void decoration_destroy(struct wlr_xdg_toplevel_decoration *decoration) {
	wlr_signal_emit_safe(&decoration->events.destroy, decoration);
	wl_list_remove(&decoration->surface_destroy.link);
	struct wlr_xdg_toplevel_decoration_configure *configure, *tmp;
	wl_list_for_each_safe(configure, tmp, &decoration->configure_list, link) {
		free(configure);
	}
	wl_resource_set_user_data(decoration->resource, NULL);
	wl_list_remove(&decoration->link);
	free(decoration);
}

static void decoration_destroy_resource(struct wl_resource *resource) {
	struct wlr_xdg_toplevel_decoration *decoration =
		wl_resource_get_user_data(resource);
	if (decoration != NULL) {
		decoration_destroy(decoration);
	}
}

static void decoration_handle_surface_destroy(struct wl_listener *listener,
		void *data) {
	struct wlr_xdg_toplevel_decoration *decoration =
		wl_container_of(listener, decoration, surface_destroy);
	wl_resource_destroy(decoration->resource);
}

static void decoration_handle_surface_configure(struct wl_listener *listener,
		void *data) {
	struct wlr_xdg_toplevel_decoration *decoration =
		wl_container_of(listener, decoration, surface_configure);
	struct wlr_xdg_surface_configure *surface_configure = data;

	if (decoration->current_mode == decoration->pending_mode) {
		return;
	}

	struct wlr_xdg_toplevel_decoration_configure *configure =
		calloc(1, sizeof(struct wlr_xdg_toplevel_decoration_configure));
	if (configure == NULL) {
		return;
	}
	configure->surface_configure = surface_configure;
	configure->mode = decoration->pending_mode;
	wl_list_insert(decoration->configure_list.prev, &configure->link);

	zxdg_toplevel_decoration_v1_send_configure(decoration->resource,
		configure->mode);
}

static void decoration_handle_surface_ack_configure(struct wl_listener *listener,
		void *data) {
	struct wlr_xdg_toplevel_decoration *decoration =
		wl_container_of(listener, decoration, surface_ack_configure);
	struct wlr_xdg_surface_configure *surface_configure = data;

	bool found = false;
	struct wlr_xdg_toplevel_decoration_configure *configure;
	wl_list_for_each(configure, &decoration->configure_list, link) {
		if (configure->surface_configure == surface_configure) {
			found = true;
			break;
		}
	}
	if (!found) {
		return;
	}

	decoration->current_mode = configure->mode;

	wl_list_remove(&configure->link);
	free(configure);
}

static void decoration_handle_surface_commit(struct wl_listener *listener,
		void *data) {
	struct wlr_xdg_toplevel_decoration *decoration =
		wl_container_of(listener, decoration, surface_commit);
	struct wlr_xdg_toplevel_decoration_manager *manager = decoration->manager;

	if (decoration->surface->added) {
		wl_list_remove(&decoration->surface_commit.link);

		decoration->added = true;
		wlr_signal_emit_safe(&manager->events.new_decoration, decoration);
	}
}

static void decoration_manager_handle_get_decoration(struct wl_client *client,
		struct wl_resource *manager_resource, uint32_t id,
		struct wl_resource *toplevel_resource) {
	struct wlr_xdg_toplevel_decoration_manager *manager =
		wl_resource_get_user_data(manager_resource);
	struct wlr_xdg_surface *surface =
		wl_resource_get_user_data(toplevel_resource);
	assert(surface->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL);

	struct wlr_xdg_toplevel_decoration *decoration =
		calloc(1, sizeof(struct wlr_xdg_toplevel_decoration));
	if (decoration == NULL) {
		wl_client_post_no_memory(client);
		return;
	}
	decoration->manager = manager;
	decoration->surface = surface;

	int version = wl_resource_get_version(manager_resource);
	decoration->resource = wl_resource_create(client,
		&zxdg_toplevel_decoration_v1_interface, version, id);
	if (decoration->resource == NULL) {
		free(decoration);
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(decoration->resource, &decoration_impl,
		decoration, decoration_destroy_resource);

	wlr_log(L_DEBUG, "new xdg_toplevel_decoration %p (res %p)", decoration,
		decoration->resource);

	wl_list_init(&decoration->configure_list);
	wl_signal_init(&decoration->events.destroy);
	wl_signal_init(&decoration->events.request_mode);

	wl_signal_add(&surface->events.destroy, &decoration->surface_destroy);
	decoration->surface_destroy.notify = decoration_handle_surface_destroy;
	wl_signal_add(&surface->events.configure, &decoration->surface_configure);
	decoration->surface_configure.notify = decoration_handle_surface_configure;
	wl_signal_add(&surface->events.ack_configure,
		&decoration->surface_ack_configure);
	decoration->surface_ack_configure.notify =
		decoration_handle_surface_ack_configure;

	wl_list_insert(&manager->decorations, &decoration->link);

	if (surface->added) {
		decoration->added = true;
		wlr_signal_emit_safe(&manager->events.new_decoration, decoration);
	} else {
		wl_signal_add(&surface->surface->events.commit,
			&decoration->surface_commit);
		decoration->surface_commit.notify = decoration_handle_surface_commit;
	}
}

static const struct zxdg_toplevel_decoration_manager_v1_interface
		decoration_manager_impl = {
	.get_decoration = decoration_manager_handle_get_decoration,
};

void decoration_manager_destroy_resource(struct wl_resource *resource) {
	wl_list_remove(wl_resource_get_link(resource));
}

static void decoration_manager_bind(struct wl_client *client, void *data,
		uint32_t version, uint32_t id) {
	struct wlr_xdg_toplevel_decoration_manager *manager = data;

	struct wl_resource *resource = wl_resource_create(client,
		&zxdg_toplevel_decoration_manager_v1_interface, version, id);
	if (resource == NULL) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(resource, &decoration_manager_impl,
		manager, decoration_manager_destroy_resource);

	wl_list_insert(&manager->wl_resources, wl_resource_get_link(resource));
}

static void handle_display_destroy(struct wl_listener *listener, void *data) {
	struct wlr_xdg_toplevel_decoration_manager *manager =
		wl_container_of(listener, manager, display_destroy);
	wlr_xdg_toplevel_decoration_manager_destroy(manager);
}

struct wlr_xdg_toplevel_decoration_manager *
		wlr_xdg_toplevel_decoration_manager_create(struct wl_display *display) {
	struct wlr_xdg_toplevel_decoration_manager *manager =
		calloc(1, sizeof(struct wlr_xdg_toplevel_decoration_manager));
	if (manager == NULL) {
		return NULL;
	}
	manager->wl_global = wl_global_create(display,
		&zxdg_toplevel_decoration_manager_v1_interface, 1, manager,
		decoration_manager_bind);
	if (manager->wl_global == NULL) {
		free(manager);
		return NULL;
	}
	wl_list_init(&manager->wl_resources);
	wl_list_init(&manager->decorations);
	wl_signal_init(&manager->events.new_decoration);

	manager->display_destroy.notify = handle_display_destroy;
	wl_display_add_destroy_listener(display, &manager->display_destroy);

	return manager;
}

void wlr_xdg_toplevel_decoration_manager_destroy(
		struct wlr_xdg_toplevel_decoration_manager *manager) {
	if (manager == NULL) {
		return;
	}
	wl_list_remove(&manager->display_destroy.link);
	struct wlr_xdg_toplevel_decoration *decoration, *tmp_decoration;
	wl_list_for_each_safe(decoration, tmp_decoration, &manager->decorations,
			link) {
		decoration_destroy(decoration);
	}
	struct wl_resource *resource, *tmp_resource;
	wl_resource_for_each_safe(resource, tmp_resource, &manager->wl_resources) {
		decoration_manager_destroy_resource(resource);
	}
	wl_global_destroy(manager->wl_global);
	free(manager);
}
