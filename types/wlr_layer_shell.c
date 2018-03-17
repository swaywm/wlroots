#include <assert.h>
#include <stdlib.h>
#include <wayland-server.h>
#include <wlr/types/wlr_layer_shell.h>
#include "wlr-layer-shell-unstable-v1-protocol.h"

static void layer_shell_handle_get_layer_surface(struct wl_client *client,
				  struct wl_resource *resource,
				  uint32_t id,
				  struct wl_resource *surface,
				  struct wl_resource *output,
				  uint32_t layer,
				  const char *namespace) {
	// TODO
}

static struct zwlr_layer_shell_v1_interface layer_shell_impl = {
	.get_layer_surface = layer_shell_handle_get_layer_surface,
};

static struct wlr_layer_client *layer_client_from_resource(
		struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource, &zwlr_layer_shell_v1_interface,
		&layer_shell_impl));
	return wl_resource_get_user_data(resource);
}

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
