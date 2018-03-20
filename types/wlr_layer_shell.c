#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-server.h>
#include <wlr/types/wlr_layer_shell.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_surface.h>
#include <wlr/util/log.h>
#include "util/signal.h"
#include "wlr-layer-shell-unstable-v1-protocol.h"

static void resource_handle_destroy(struct wl_client *client,
		struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static const struct zwlr_layer_shell_v1_interface layer_shell_implementation;
static const struct zwlr_layer_surface_v1_interface layer_surface_implementation;

static struct wlr_layer_client *layer_client_from_resource(
		struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource, &zwlr_layer_shell_v1_interface,
		&layer_shell_implementation));
	return wl_resource_get_user_data(resource);
}

static struct wlr_layer_surface *layer_surface_from_resource(
		struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource, &zwlr_layer_surface_v1_interface,
		&layer_surface_implementation));
	return wl_resource_get_user_data(resource);
}

static void layer_surface_configure_destroy(
		struct wlr_layer_surface_configure *configure) {
	if (configure == NULL) {
		return;
	}
	wl_list_remove(&configure->link);
	free(configure);
}

static void layer_surface_handle_ack_configure(struct wl_client *client,
		struct wl_resource *resource, uint32_t serial) {
	struct wlr_layer_surface *surface = layer_surface_from_resource(resource);

	bool found = false;
	struct wlr_layer_surface_configure *configure, *tmp;
	wl_list_for_each_safe(configure, tmp, &surface->configure_list, link) {
		if (configure->serial < serial) {
			layer_surface_configure_destroy(configure);
		} else if (configure->serial == serial) {
			found = true;
			break;
		} else {
			break;
		}
	}
	if (!found) {
		wl_resource_post_error(surface->client->resource,
			ZWLR_LAYER_SURFACE_V1_ERROR_INVALID_SURFACE_STATE,
			"wrong configure serial: %u", serial);
		return;
	}

	surface->configured = true;
	surface->configure_serial = serial;

	surface->current.anchor = configure->state.anchor;
	surface->current.exclusive_zone = configure->state.exclusive_zone;
	surface->current.margin = configure->state.margin;
	surface->current.width = configure->state.width;
	surface->current.height = configure->state.height;

	layer_surface_configure_destroy(configure);
}

static void layer_surface_handle_set_size(struct wl_client *client,
		struct wl_resource *resource, int32_t width, int32_t height) {
	struct wlr_layer_surface *surface = layer_surface_from_resource(resource);
	surface->client_pending.width = width;
	surface->client_pending.height = height;
}

static void layer_surface_handle_set_anchor(struct wl_client *client,
		struct wl_resource *resource, uint32_t anchor) {
	const uint32_t max_anchor =
		ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
		ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
		ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
		ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
	if (anchor > max_anchor) {
		wl_resource_post_error(resource,
			ZWLR_LAYER_SURFACE_V1_ERROR_INVALID_ANCHOR,
			"invalid anchor %d", anchor);
	}
	struct wlr_layer_surface *surface = layer_surface_from_resource(resource);
	surface->client_pending.anchor = anchor;
}

static void layer_surface_handle_set_exclusive_zone(struct wl_client *client,
		struct wl_resource *resource, uint32_t zone) {
	struct wlr_layer_surface *surface = layer_surface_from_resource(resource);
	surface->client_pending.exclusive_zone = zone;
}

static void layer_surface_handle_set_margin(
		struct wl_client *client, struct wl_resource *resource,
		int32_t top, int32_t right, int32_t bottom, int32_t left) {
	struct wlr_layer_surface *surface = layer_surface_from_resource(resource);
	surface->client_pending.margin.top = top;
	surface->client_pending.margin.right = right;
	surface->client_pending.margin.bottom = bottom;
	surface->client_pending.margin.left = left;
}

static void layer_surface_handle_set_keyboard_interactivity(
		struct wl_client *client, struct wl_resource *resource,
		uint32_t interactive) {
	struct wlr_layer_surface *surface = layer_surface_from_resource(resource);
	surface->client_pending.keyboard_interactive = interactive == 1;
}

static void layer_surface_handle_get_popup(struct wl_client *client,
		struct wl_resource *resource, struct wl_resource *popup) {
	// TODO
}

static const struct zwlr_layer_surface_v1_interface layer_surface_implementation = {
	.destroy = resource_handle_destroy,
	.ack_configure = layer_surface_handle_ack_configure,
	.set_size = layer_surface_handle_set_size,
	.set_anchor = layer_surface_handle_set_anchor,
	.set_exclusive_zone = layer_surface_handle_set_exclusive_zone,
	.set_margin = layer_surface_handle_set_margin,
	.set_keyboard_interactivity = layer_surface_handle_set_keyboard_interactivity,
	.get_popup = layer_surface_handle_get_popup,
};

static void layer_surface_unmap(struct wlr_layer_surface *surface) {
	// TODO: probably need to ungrab before this event
	wlr_signal_emit_safe(&surface->events.unmap, surface);

	struct wlr_layer_surface_configure *configure, *tmp;
	wl_list_for_each_safe(configure, tmp, &surface->configure_list, link) {
		layer_surface_configure_destroy(configure);
	}

	surface->added = surface->configured = surface->mapped = false;
	surface->configure_serial = 0;
	if (surface->configure_idle) {
		wl_event_source_remove(surface->configure_idle);
		surface->configure_idle = NULL;
	}
	surface->configure_next_serial = 0;
}

static void layer_surface_destroy(struct wlr_layer_surface *surface) {
	layer_surface_unmap(surface);
	wlr_signal_emit_safe(&surface->events.destroy, surface);
	wl_resource_set_user_data(surface->resource, NULL);
	wl_list_remove(&surface->link);
	wl_list_remove(&surface->surface_destroy_listener.link);
	wlr_surface_set_role_committed(surface->surface, NULL, NULL);
	free(surface);
}

static void layer_surface_resource_destroy(struct wl_resource *resource) {
	struct wlr_layer_surface *surface =
		layer_surface_from_resource(resource);
	if (surface != NULL) {
		layer_surface_destroy(surface);
	}
}

static bool wlr_layer_surface_state_changed(struct wlr_layer_surface *surface) {
	if (!surface->configured) {
		return true;
	}

	struct wlr_layer_surface_state *state;
	if (wl_list_empty(&surface->configure_list)) {
		state = &surface->current;
	} else {
		struct wlr_layer_surface_configure *configure =
			wl_container_of(surface->configure_list.prev, configure, link);
		state = &configure->state;
	}

	return !memcmp(state, &surface->server_pending,
			sizeof(struct wlr_layer_surface_state));
}

static void wlr_layer_surface_send_configure(void *user_data) {
	struct wlr_layer_surface *surface = user_data;
	surface->configure_idle = NULL;
	struct wlr_layer_surface_configure *configure =
		calloc(1, sizeof(struct wlr_layer_surface_configure));
	if (configure == NULL) {
		wl_client_post_no_memory(surface->client->client);
		return;
	}

	wl_list_insert(surface->configure_list.prev, &configure->link);
	configure->serial = surface->configure_next_serial;
	configure->state = surface->server_pending;

	zwlr_layer_surface_v1_send_configure(surface->resource,
			configure->serial, configure->state.width, configure->state.height);
}

static uint32_t wlr_layer_surface_schedule_configure(
		struct wlr_layer_surface *surface) {
	struct wl_display *display = wl_client_get_display(surface->client->client);
	struct wl_event_loop *loop = wl_display_get_event_loop(display);
	bool changed = wlr_layer_surface_state_changed(surface);

	if (surface->configure_idle != NULL) {
		if (changed) {
			// configure request already scheduled
			return surface->configure_next_serial;
		}
		// configure request not necessary anymore
		wl_event_source_remove(surface->configure_idle);
		surface->configure_idle = NULL;
		return 0;
	} else {
		if (!changed) {
			// configure request not necessary
			return 0;
		}
		surface->configure_next_serial = wl_display_next_serial(display);
		surface->configure_idle = wl_event_loop_add_idle(loop,
			wlr_layer_surface_send_configure, surface);
		return surface->configure_next_serial;
	}
}

void wlr_layer_surface_configure(struct wlr_layer_surface *surface,
		uint32_t width, uint32_t height) {
	surface->server_pending.width = width;
	surface->server_pending.height = height;
	wlr_layer_surface_schedule_configure(surface);
}

static void handle_wlr_surface_committed(struct wlr_surface *wlr_surface,
		void *role_data) {
	struct wlr_layer_surface *surface = role_data;

	if (wlr_surface_has_buffer(surface->surface) && !surface->configured) {
		wl_resource_post_error(surface->resource,
			ZWLR_LAYER_SHELL_V1_ERROR_ALREADY_CONSTRUCTED,
			"layer_surface has never been configured");
		return;
	}

	surface->current.anchor = surface->client_pending.anchor;
	surface->current.exclusive_zone = surface->client_pending.exclusive_zone;
	surface->current.margin = surface->client_pending.margin;
	surface->current.keyboard_interactive =
		surface->client_pending.keyboard_interactive;
	surface->current.width = surface->client_pending.width;
	surface->current.height = surface->client_pending.height;

	if (!surface->added) {
		surface->added = true;
		wlr_signal_emit_safe(&surface->client->shell->events.new_surface,
				surface);
	}
	if (surface->configured && wlr_surface_has_buffer(surface->surface) &&
			!surface->mapped) {
		surface->mapped = true;
		wlr_signal_emit_safe(&surface->events.map, surface);
	}
	if (surface->configured && !wlr_surface_has_buffer(surface->surface) &&
			surface->mapped) {
		layer_surface_unmap(surface);
	}
}

static void handle_wlr_surface_destroyed(struct wl_listener *listener,
		void *data) {
	struct wlr_layer_surface *layer_surface =
		wl_container_of(listener, layer_surface, surface_destroy_listener);
	layer_surface_destroy(layer_surface);
}

static void layer_shell_handle_get_layer_surface(struct wl_client *wl_client,
		struct wl_resource *client_resource, uint32_t id,
		struct wl_resource *surface_resource,
		struct wl_resource *output_resource,
		uint32_t layer, const char *namespace) {
	struct wlr_layer_client *client =
		layer_client_from_resource(client_resource);

	struct wlr_layer_surface *surface =
		calloc(1, sizeof(struct wlr_layer_surface));
	if (surface == NULL) {
		wl_client_post_no_memory(wl_client);
		return;
	}

	surface->client = client;
	surface->surface = wlr_surface_from_resource(surface_resource);
	surface->output = wlr_output_from_resource(output_resource);
	surface->resource = wl_resource_create(wl_client,
		&zwlr_layer_surface_v1_interface,
		wl_resource_get_version(client_resource),
		id);
	surface->namespace = strdup(namespace);
	surface->layer = layer;
	if (surface->resource == NULL || surface->namespace == NULL) {
		free(surface);
		wl_client_post_no_memory(wl_client);
		return;
	}
	if (layer > ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY) {
		wl_resource_post_error(surface->resource,
				ZWLR_LAYER_SHELL_V1_ERROR_INVALID_LAYER,
				"Invalid layer %d", layer);
		free(surface);
		return;
	}

	wl_list_init(&surface->configure_list);

	wl_signal_init(&surface->events.destroy);
	wl_signal_add(&surface->surface->events.destroy,
		&surface->surface_destroy_listener);
	surface->surface_destroy_listener.notify = handle_wlr_surface_destroyed;
	wl_signal_init(&surface->events.map);
	wl_signal_init(&surface->events.unmap);

	wlr_surface_set_role_committed(surface->surface,
		handle_wlr_surface_committed, surface);

	wlr_log(L_DEBUG, "new layer_surface %p (res %p)",
			surface, surface->resource);
	wl_resource_set_implementation(surface->resource,
		&layer_surface_implementation, surface, layer_surface_resource_destroy);
	wl_list_insert(&client->surfaces, &surface->link);
}

static const struct zwlr_layer_shell_v1_interface layer_shell_implementation = {
	.get_layer_surface = layer_shell_handle_get_layer_surface,
};

static void wlr_layer_client_destroy(struct wl_resource *resource) {
	struct wlr_layer_client *client = layer_client_from_resource(resource);

	struct wlr_layer_surface *surface, *tmp = NULL;
	wl_list_for_each_safe(surface, tmp, &client->surfaces, link) {
		layer_surface_destroy(surface);
	}

	wl_list_remove(&client->link);
	free(client);
}

static void layer_shell_bind(struct wl_client *wl_client, void *data,
		uint32_t version, uint32_t id) {
	struct wlr_layer_shell *layer_shell = data;
	assert(wl_client && layer_shell);

	struct wlr_layer_client *client =
		calloc(1, sizeof(struct wlr_layer_client));
	if (client == NULL) {
		wl_client_post_no_memory(wl_client);
		return;
	}

	wl_list_init(&client->surfaces);

	client->resource = wl_resource_create(
			wl_client, &zwlr_layer_shell_v1_interface, version, id);
	if (client->resource == NULL) {
		free(client);
		wl_client_post_no_memory(wl_client);
		return;
	}
	client->client = wl_client;
	client->shell = layer_shell;

	wl_resource_set_implementation(client->resource,
			&layer_shell_implementation, client, wlr_layer_client_destroy);
	wl_list_insert(&layer_shell->clients, &client->link);
}

static void handle_display_destroy(struct wl_listener *listener, void *data) {
	struct wlr_layer_shell *layer_shell =
		wl_container_of(listener, layer_shell, display_destroy);
	wlr_layer_shell_destroy(layer_shell);
}

struct wlr_layer_shell *wlr_layer_shell_create(struct wl_display *display) {
	struct wlr_layer_shell *layer_shell =
		calloc(1, sizeof(struct wlr_layer_shell));
	if (!layer_shell) {
		return NULL;
	}

	wl_list_init(&layer_shell->clients);

	struct wl_global *wl_global = wl_global_create(display,
		&zwlr_layer_shell_v1_interface, 1, layer_shell, layer_shell_bind);
	if (!wl_global) {
		free(layer_shell);
		return NULL;
	}
	layer_shell->wl_global = wl_global;

	wl_signal_init(&layer_shell->events.new_surface);

	layer_shell->display_destroy.notify = handle_display_destroy;
	wl_display_add_destroy_listener(display, &layer_shell->display_destroy);

	return layer_shell;
}

void wlr_layer_shell_destroy(struct wlr_layer_shell *layer_shell) {
	if (!layer_shell) {
		return;
	}
	wl_list_remove(&layer_shell->display_destroy.link);
	wl_global_destroy(layer_shell->wl_global);
	free(layer_shell);
}
