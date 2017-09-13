#ifndef _WLR_XDG_SHELL_V6_H
#define _WLR_XDG_SHELL_V6_H
#include <wlr/types/wlr_box.h>
#include <wayland-server.h>

struct wlr_xdg_shell_v6 {
	struct wl_global *wl_global;
	struct wl_list wl_resources;
	struct wl_list surfaces;

	void *data;
};

enum wlr_xdg_surface_v6_role {
	WLR_XDG_SURFACE_V6_ROLE_NONE,
	WLR_XDG_SURFACE_V6_ROLE_TOPLEVEL,
	WLR_XDG_SURFACE_V6_ROLE_POPUP,
};

struct wlr_xdg_toplevel_v6_state {
	bool maximized;
	bool fullscreen;
	bool resizing;
	bool activated;

	uint32_t max_width;
	uint32_t max_height;

	uint32_t min_width;
	uint32_t min_height;
};

struct wlr_xdg_toplevel_v6 {
	struct wlr_xdg_toplevel_v6_state next;
	struct wlr_xdg_toplevel_v6_state current;
};

struct wlr_xdg_surface_v6 {
	struct wl_resource *resource;
	struct wlr_surface *surface;
	struct wl_list link;
	enum wlr_xdg_surface_v6_role role;
	struct wlr_xdg_toplevel_v6 *toplevel_state;

	char *title;
	char *app_id;

	bool has_next_geometry;
	struct wlr_box *next_geometry;
	struct wlr_box *geometry;

	struct wl_listener surface_destroy_listener;
	struct wl_listener surface_commit_listener;

	void *data;
};

struct wlr_xdg_shell_v6 *wlr_xdg_shell_v6_create(struct wl_display *display);
void wlr_xdg_shell_v6_destroy(struct wlr_xdg_shell_v6 *xdg_shell);

#endif
