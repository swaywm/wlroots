#ifndef WLR_TYPES_WLR_LAYER_SHELL_H
#define WLR_TYPES_WLR_LAYER_SHELL_H

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

struct wlr_layer_shell *wlr_layer_shell_create(struct wl_display *display);
void wlr_layer_shell_destroy(struct wlr_layer_shell *layer_shell);

#endif
