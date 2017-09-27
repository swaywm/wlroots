#ifndef WLR_TYPES_WLR_WL_SHELL_H
#define WLR_TYPES_WLR_WL_SHELL_H

#include <wayland-server.h>

struct wlr_wl_shell {
	struct wl_global *wl_global;
	struct wl_list wl_resources;
	struct wl_list surfaces;
	uint32_t ping_timeout;

	struct {
		struct wl_signal new_surface;
	} events;

	void *data;
};

struct wlr_wl_shell_surface {
	struct wlr_wl_shell *shell;
	struct wl_client *client;
	struct wl_resource *resource;
	struct wlr_surface *surface;
	struct wl_list link;

	uint32_t ping_serial;
	struct wl_event_source *ping_timer;

	struct {
		struct wl_signal ping_timeout;
	} events;

	void *data;
};

struct wlr_wl_shell *wlr_wl_shell_create(struct wl_display *display);
void wlr_wl_shell_destroy(struct wlr_wl_shell *wlr_wl_shell);

void wlr_wl_shell_surface_ping(struct wlr_wl_shell_surface *surface);

#endif
