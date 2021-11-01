#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/util/log.h>
#include "util/signal.h"
#include "xdg-decoration-unstable-v1-protocol.h"

#define DECORATION_MANAGER_VERSION 1

static const struct zxdg_toplevel_decoration_v1_interface
	toplevel_decoration_impl;

static struct wlr_xdg_toplevel_decoration_v1 *toplevel_decoration_from_resource(
		struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource,
		&zxdg_toplevel_decoration_v1_interface, &toplevel_decoration_impl));
	return wl_resource_get_user_data(resource);
}

static void toplevel_decoration_handle_destroy(struct wl_client *client,
		struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static void toplevel_decoration_handle_set_mode(struct wl_client *client,
		struct wl_resource *resource,
		enum zxdg_toplevel_decoration_v1_mode mode) {
	struct wlr_xdg_toplevel_decoration_v1 *decoration =
		toplevel_decoration_from_resource(resource);

	decoration->requested_mode =
		(enum wlr_xdg_toplevel_decoration_v1_mode)mode;
	wlr_signal_emit_safe(&decoration->events.request_mode, decoration);
}

static void toplevel_decoration_handle_unset_mode(struct wl_client *client,
		struct wl_resource *resource) {
	struct wlr_xdg_toplevel_decoration_v1 *decoration =
		toplevel_decoration_from_resource(resource);

	decoration->requested_mode = WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_NONE;
	wlr_signal_emit_safe(&decoration->events.request_mode, decoration);
}

static const struct zxdg_toplevel_decoration_v1_interface
		toplevel_decoration_impl = {
	.destroy = toplevel_decoration_handle_destroy,
	.set_mode = toplevel_decoration_handle_set_mode,
	.unset_mode = toplevel_decoration_handle_unset_mode,
};

uint32_t wlr_xdg_toplevel_decoration_v1_set_mode(
		struct wlr_xdg_toplevel_decoration_v1 *decoration,
		enum wlr_xdg_toplevel_decoration_v1_mode mode) {
	assert(mode != WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_NONE);
	decoration->scheduled_mode = mode;
	return wlr_xdg_surface_schedule_configure(decoration->surface);
}

static void toplevel_decoration_handle_resource_destroy(
		struct wl_resource *resource) {
	struct wlr_xdg_toplevel_decoration_v1 *decoration =
		toplevel_decoration_from_resource(resource);
	wlr_signal_emit_safe(&decoration->events.destroy, decoration);
	wlr_surface_synced_finish(&decoration->synced);
	wl_list_remove(&decoration->surface_commit.link);
	wl_list_remove(&decoration->surface_configure.link);
	wl_list_remove(&decoration->surface_ack_configure.link);
	struct wlr_xdg_toplevel_decoration_v1_configure *configure, *tmp;
	wl_list_for_each_safe(configure, tmp, &decoration->configure_list, link) {
		free(configure);
	}
	wl_list_remove(&decoration->link);
	free(decoration);
}

static void toplevel_decoration_handle_surface_configure(
		struct wl_listener *listener, void *data) {
	struct wlr_xdg_toplevel_decoration_v1 *decoration =
		wl_container_of(listener, decoration, surface_configure);
	struct wlr_xdg_surface_configure *surface_configure = data;

	if (decoration->pending.mode == decoration->scheduled_mode) {
		return;
	}

	struct wlr_xdg_toplevel_decoration_v1_configure *configure =
		calloc(1, sizeof(struct wlr_xdg_toplevel_decoration_v1_configure));
	if (configure == NULL) {
		return;
	}
	configure->surface_configure = surface_configure;
	configure->mode = decoration->scheduled_mode;
	wl_list_insert(decoration->configure_list.prev, &configure->link);

	zxdg_toplevel_decoration_v1_send_configure(decoration->resource,
		configure->mode);
}

static void toplevel_decoration_handle_surface_ack_configure(
		struct wl_listener *listener, void *data) {
	struct wlr_xdg_toplevel_decoration_v1 *decoration =
		wl_container_of(listener, decoration, surface_ack_configure);
	struct wlr_xdg_surface_configure *surface_configure = data;

	// First find the ack'ed configure
	bool found = false;
	struct wlr_xdg_toplevel_decoration_v1_configure *configure, *tmp;
	wl_list_for_each(configure, &decoration->configure_list, link) {
		if (configure->surface_configure == surface_configure) {
			found = true;
			break;
		}
	}
	if (!found) {
		return;
	}
	// Then remove old configures from the list
	wl_list_for_each_safe(configure, tmp, &decoration->configure_list, link) {
		if (configure->surface_configure == surface_configure) {
			break;
		}
		wl_list_remove(&configure->link);
		free(configure);
	}

	decoration->pending.mode = configure->mode;

	wl_list_remove(&configure->link);
	free(configure);
}

static void toplevel_decoration_handle_surface_commit(
		struct wl_listener *listener, void *data) {
	struct wlr_xdg_toplevel_decoration_v1 *decoration =
		wl_container_of(listener, decoration, surface_commit);
	struct wlr_xdg_decoration_manager_v1 *manager = decoration->manager;

	if (decoration->surface->added && !decoration->added) {
		decoration->added = true;
		wlr_signal_emit_safe(&manager->events.new_toplevel_decoration,
			decoration);
	}
}

static void xdg_toplevel_decoration_synced_destroy(struct wlr_surface_synced *synced) {
	struct wlr_xdg_toplevel_decoration_v1 *decoration =
		wl_container_of(synced, decoration, synced);
	wl_resource_destroy(decoration->resource);
}

static void xdg_toplevel_decoration_synced_squash_state(
		struct wlr_surface_synced_state *synced_state,
		struct wlr_surface_synced_state *synced_prev) {
	struct wlr_xdg_toplevel_decoration_v1_state *state =
		wl_container_of(synced_state, state, synced_state);
	struct wlr_xdg_toplevel_decoration_v1_state *prev =
		wl_container_of(synced_prev, prev, synced_state);

	prev->mode = state->mode;
}

static struct wlr_surface_synced_state *xdg_toplevel_decoration_synced_create_state(void) {
	struct wlr_xdg_toplevel_decoration_v1_state *state =
		calloc(1, sizeof(*state));
	if (!state) {
		return NULL;
	}
	return &state->synced_state;
}

static void xdg_toplevel_decoration_synced_destroy_state(
		struct wlr_surface_synced_state *synced_state) {
	struct wlr_xdg_toplevel_decoration_v1_state *state =
		wl_container_of(synced_state, state, synced_state);
	free(state);
}

static const struct wlr_surface_synced_interface xdg_toplevel_decoration_synced_impl = {
	.name = "wlr_xdg_toplevel_decoration",
	.destroy = xdg_toplevel_decoration_synced_destroy,
	.squash_state = xdg_toplevel_decoration_synced_squash_state,
	.create_state = xdg_toplevel_decoration_synced_create_state,
	.destroy_state = xdg_toplevel_decoration_synced_destroy_state,
};

static const struct zxdg_decoration_manager_v1_interface decoration_manager_impl;

static struct wlr_xdg_decoration_manager_v1 *
		decoration_manager_from_resource(struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource,
		&zxdg_decoration_manager_v1_interface,
		&decoration_manager_impl));
	return wl_resource_get_user_data(resource);
}

static void decoration_manager_handle_destroy(
		struct wl_client *client, struct wl_resource *manager_resource) {
	wl_resource_destroy(manager_resource);
}

static void decoration_manager_handle_get_toplevel_decoration(
		struct wl_client *client, struct wl_resource *manager_resource,
		uint32_t id, struct wl_resource *toplevel_resource) {
	struct wlr_xdg_decoration_manager_v1 *manager =
		decoration_manager_from_resource(manager_resource);
	struct wlr_xdg_surface *surface =
		wlr_xdg_surface_from_toplevel_resource(toplevel_resource);
	assert(surface->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL);

	if (wlr_surface_has_buffer(surface->surface)) {
		wl_resource_post_error(manager_resource,
			ZXDG_TOPLEVEL_DECORATION_V1_ERROR_UNCONFIGURED_BUFFER,
			"xdg_toplevel_decoration must not have a buffer at creation");
		return;
	}

	struct wlr_xdg_toplevel_decoration_v1 *decoration =
		calloc(1, sizeof(struct wlr_xdg_toplevel_decoration_v1));
	if (decoration == NULL) {
		wl_client_post_no_memory(client);
		return;
	}
	decoration->manager = manager;
	decoration->surface = surface;

	if (!wlr_surface_synced_init(&decoration->synced,
			&xdg_toplevel_decoration_synced_impl, surface->surface,
			&decoration->current.synced_state,
			&decoration->pending.synced_state)) {
		free(decoration);
		wl_client_post_no_memory(client);
		return;
	}

	uint32_t version = wl_resource_get_version(manager_resource);
	decoration->resource = wl_resource_create(client,
		&zxdg_toplevel_decoration_v1_interface, version, id);
	if (decoration->resource == NULL) {
		wlr_surface_synced_finish(&decoration->synced);
		free(decoration);
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(decoration->resource,
		&toplevel_decoration_impl, decoration,
		toplevel_decoration_handle_resource_destroy);

	wlr_log(WLR_DEBUG, "new xdg_toplevel_decoration %p (res %p)", decoration,
		decoration->resource);

	wl_list_init(&decoration->configure_list);
	wl_signal_init(&decoration->events.destroy);
	wl_signal_init(&decoration->events.request_mode);

	wl_signal_add(&surface->events.configure, &decoration->surface_configure);
	decoration->surface_configure.notify =
		toplevel_decoration_handle_surface_configure;
	wl_signal_add(&surface->events.ack_configure,
		&decoration->surface_ack_configure);
	decoration->surface_ack_configure.notify =
		toplevel_decoration_handle_surface_ack_configure;
	wl_signal_add(&surface->surface->events.commit,
		&decoration->surface_commit);
	decoration->surface_commit.notify =
		toplevel_decoration_handle_surface_commit;

	wl_list_insert(&manager->decorations, &decoration->link);

	if (surface->added) {
		decoration->added = true;
		wlr_signal_emit_safe(&manager->events.new_toplevel_decoration,
			decoration);
	}
}

static const struct zxdg_decoration_manager_v1_interface
		decoration_manager_impl = {
	.destroy = decoration_manager_handle_destroy,
	.get_toplevel_decoration = decoration_manager_handle_get_toplevel_decoration,
};

static void decoration_manager_bind(struct wl_client *client, void *data,
		uint32_t version, uint32_t id) {
	struct wlr_xdg_decoration_manager_v1 *manager = data;

	struct wl_resource *resource = wl_resource_create(client,
		&zxdg_decoration_manager_v1_interface, version, id);
	if (resource == NULL) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(resource, &decoration_manager_impl,
		manager, NULL);
}

static void handle_display_destroy(struct wl_listener *listener, void *data) {
	struct wlr_xdg_decoration_manager_v1 *manager =
		wl_container_of(listener, manager, display_destroy);
	wlr_signal_emit_safe(&manager->events.destroy, manager);
	wl_list_remove(&manager->display_destroy.link);
	wl_global_destroy(manager->global);
	free(manager);
}

struct wlr_xdg_decoration_manager_v1 *
		wlr_xdg_decoration_manager_v1_create(struct wl_display *display) {
	struct wlr_xdg_decoration_manager_v1 *manager =
		calloc(1, sizeof(struct wlr_xdg_decoration_manager_v1));
	if (manager == NULL) {
		return NULL;
	}
	manager->global = wl_global_create(display,
		&zxdg_decoration_manager_v1_interface, DECORATION_MANAGER_VERSION,
		manager, decoration_manager_bind);
	if (manager->global == NULL) {
		free(manager);
		return NULL;
	}
	wl_list_init(&manager->decorations);
	wl_signal_init(&manager->events.new_toplevel_decoration);
	wl_signal_init(&manager->events.destroy);

	manager->display_destroy.notify = handle_display_destroy;
	wl_display_add_destroy_listener(display, &manager->display_destroy);

	return manager;
}
