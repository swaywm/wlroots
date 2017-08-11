#ifndef _WLR_XDG_SHELL_V6_H
#define _WLR_XDG_SHELL_V6_H
#include <wayland-server.h>

struct wlr_xdg_shell_v6 {
	struct wl_global *wl_global;
	struct wl_list wl_resources;
	struct wl_list surfaces;

	void *data;
};

struct wlr_xdg_surface_v6 {
	struct wl_resource *resource;
	struct wl_resource *surface;
	struct wl_list link;

	void *data;
};

struct wlr_xdg_shell_v6 *wlr_xdg_shell_v6_init(struct wl_display *display);
void wlr_xdg_shell_v6_destroy(struct wlr_xdg_shell_v6 *xdg_shell);

#endif
