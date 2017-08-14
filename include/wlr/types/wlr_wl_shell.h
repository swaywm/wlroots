#ifndef _WLR_WL_SHELL_H
#define _WLR_WL_SHELL_H
#include <wayland-server.h>

struct wlr_wl_shell {
	struct wl_global *wl_global;
	struct wl_list wl_resources;

	void *data;
};

struct wlr_wl_shell_surface {
	struct wlr_texture *wlr_texture;

	void *data;
};


void wlr_wl_shell_init(struct wlr_wl_shell *wlr_wl_shell,
		struct wl_display *display);
void wlr_wl_shell_destroy(struct wlr_wl_shell *wlr_wl_shell);

#endif
