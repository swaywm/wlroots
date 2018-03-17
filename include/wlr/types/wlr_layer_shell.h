#ifndef WLR_TYPES_WLR_LAYER_SHELL_H
#define WLR_TYPES_WLR_LAYER_SHELL_H
#include <stdbool.h>
#include <stdint.h>
#include <wayland-server.h>
#include <wlr/types/wlr_box.h>
#include <wlr/types/wlr_surface.h>
#include "wlr-layer-shell-unstable-v1-protocol.h"

struct wlr_layer_shell {
	struct wl_global *wl_global;
	struct wl_list clients;

	struct wl_listener display_destroy;

	struct {
		struct wl_signal new_surface;
	} events;

	void *data;
};

struct wlr_layer_client {
	struct wlr_layer_shell *shell;
	struct wl_resource *resource;
	struct wl_client *client;
	struct wl_list surfaces;

	struct wl_list link; // wlr_layer_shell::clients
};

struct wlr_layer_surface_state {
	uint32_t anchor;
	uint32_t exclusive_zone;
	struct {
		uint32_t top, right, bottom, left;
	} margin;
};

struct wlr_layer_surface_configure {
	struct wl_list link; // wlr_layer_surface::configure_list
	uint32_t serial;
	struct wlr_layer_surface_state *state;
};

struct wlr_layer_surface {
	struct wlr_surface *surface;
	struct wlr_layer_client *client;
	struct wl_resource *resource;
	struct wl_list link; // wlr_layer_client:surfaces

	const char *namespace;
	enum zwlr_layer_shell_v1_layer layer;

	bool added, configured, mapped;
	uint32_t configure_serial;
	struct wl_event_source *configure_idle;
	uint32_t configure_next_serial;
	struct wl_list configure_list;

	struct wlr_layer_surface_state next; // client protocol requests
	struct wlr_layer_surface_state pending; // our configure requests
	struct wlr_layer_surface_state current;

	struct wl_listener surface_destroy_listener;

	struct {
		struct wl_signal destroy;
		struct wl_signal map;
		struct wl_signal unmap;
	} events;

	void *data;
};

struct wlr_layer_shell *wlr_layer_shell_create(struct wl_display *display);
void wlr_layer_shell_destroy(struct wlr_layer_shell *layer_shell);

#endif
