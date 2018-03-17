#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-server.h>
#include <wlr/types/wlr_layer_shell.h>
#include <wlr/types/wlr_surface.h>
#include <wlr/util/log.h>
#include "util/signal.h"
#include "wlr-layer-shell-unstable-v1-protocol.h"

static void resource_handle_destroy(struct wl_client *client,
		struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static struct zwlr_layer_shell_v1_interface layer_shell_impl;
static struct zwlr_layer_surface_v1_interface layer_surface_impl;

static struct wlr_layer_client *layer_client_from_resource(
		struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource, &zwlr_layer_shell_v1_interface,
		&layer_shell_impl));
	return wl_resource_get_user_data(resource);
}

static struct wlr_layer_surface *layer_surface_from_resource(
		struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource, &zwlr_layer_surface_v1_interface,
		&layer_surface_impl));
	return wl_resource_get_user_data(resource);
}

static void layer_surface_handle_ack_configure(struct wl_client *client,
		struct wl_resource *resource, uint32_t serial) {
	// TODO
}

static void layer_surface_handle_set_anchor(struct wl_client *client,
		struct wl_resource *resource, uint32_t anchor) {
	// TODO
}

static void layer_surface_handle_set_exclusive_zone(struct wl_client *client,
		struct wl_resource *resource, uint32_t zone) {
	// TODO
}

static void layer_surface_handle_set_margin(struct wl_client *client,
		struct wl_resource *resource, int32_t top, int32_t right,
		int32_t bottom, int32_t left) {
	// TODO
}

static void layer_surface_handle_get_popup(struct wl_client *client,
		struct wl_resource *resource, struct wl_resource *popup) {
	// TODO
}

static void layer_surface_handle_get_input(struct wl_client *client,
		struct wl_resource *resource, uint32_t id, struct wl_resource *seat) {
	// TODO
}

static const struct zwlr_layer_surface_v1_interface layer_surface_implementation = {
	.destroy = resource_handle_destroy,
	.ack_configure = layer_surface_handle_ack_configure,
	.set_anchor = layer_surface_handle_set_anchor,
	.set_exclusive_zone = layer_surface_handle_set_exclusive_zone,
	.set_margin = layer_surface_handle_set_margin,
	.get_popup = layer_surface_handle_get_popup,
	.get_input = layer_surface_handle_get_input,
};

static void layer_surface_configure_destroy(
		struct wlr_layer_surface_configure *configure) {
	if (configure == NULL) {
		return;
	}
	wl_list_remove(&configure->link);
	free(configure->state);
	free(configure);
}

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

static void handle_wlr_surface_committed(struct wlr_surface *wlr_surface,
		void *role_data) {
	struct wlr_layer_surface *surface = role_data;

	if (wlr_surface_has_buffer(surface->surface) && !surface->configured) {
		wl_resource_post_error(surface->resource,
			ZWLR_LAYER_SHELL_V1_ERROR_ALREADY_CONSTRUCTED,
			"layer_surface has never been configured");
		return;
	}
	if (!surface->added) {
		surface->added = true;
		wlr_signal_emit_safe(
				&surface->client->shell->events.new_surface, surface);
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

	wl_signal_init(&surface->events.destroy);
	wl_signal_add(&surface->surface->events.destroy,
		&surface->surface_destroy_listener);
	surface->surface_destroy_listener.notify = handle_wlr_surface_destroyed;

	wlr_surface_set_role_committed(surface->surface,
		handle_wlr_surface_committed, surface);

	wlr_log(L_DEBUG, "new layer_surface %p (res %p)",
			surface, surface->resource);
	wl_resource_set_implementation(surface->resource,
		&layer_surface_implementation, surface, layer_surface_resource_destroy);
	wl_list_insert(&client->surfaces, &surface->link);
}

static struct zwlr_layer_shell_v1_interface layer_shell_impl = {
	.get_layer_surface = layer_shell_handle_get_layer_surface,
};

static void wlr_layer_client_destroy(struct wl_resource *resource) {
	struct wlr_layer_client *client = layer_client_from_resource(resource);

	// TODO: Destroy surfaces

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

	wl_resource_set_implementation(client->resource, &layer_shell_impl, client,
		wlr_layer_client_destroy);
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
