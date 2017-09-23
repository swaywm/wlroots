#ifndef WLR_TYPES_WLR_WL_SHELL_H
#define WLR_TYPES_WLR_WL_SHELL_H

#include <wayland-server.h>

struct wlr_wl_shell {
	struct wl_global *wl_global;
	struct wl_list wl_resources;
	struct wl_list surfaces;

	void *data;
};

struct wlr_wl_shell_surface {
	struct wl_resource *surface;
	struct wlr_texture *wlr_texture;
	struct wl_list link;

	void *data;
};


struct wlr_wl_shell *wlr_wl_shell_create(struct wl_display *display);
void wlr_wl_shell_destroy(struct wlr_wl_shell *wlr_wl_shell);

#endif
