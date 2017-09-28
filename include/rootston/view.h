#ifndef _ROOTSTON_VIEW_H
#define _ROOTSTON_VIEW_H
#include <stdbool.h>
#include <wlr/types/wlr_box.h>
#include <wlr/types/wlr_surface.h>
#include <wlr/types/wlr_xdg_shell_v6.h>

struct roots_wl_shell_surface {
	struct roots_view *view;
	// TODO: Maybe destroy listener should go in roots_view
	struct wl_listener destroy;
	struct wl_listener ping_timeout;
	struct wl_listener request_move;
	struct wl_listener request_resize;
	struct wl_listener request_set_fullscreen;
	struct wl_listener request_set_maximized;
};

struct roots_xdg_surface_v6 {
	struct roots_view *view;
	// TODO: Maybe destroy listener should go in roots_view
	struct wl_listener destroy;
	struct wl_listener ping_timeout;
	struct wl_listener request_minimize;
	struct wl_listener request_move;
	struct wl_listener request_resize;
	struct wl_listener request_show_window_menu;
};

struct roots_xwayland_surface {
	struct roots_view *view;
	// TODO: Maybe destroy listener should go in roots_view
	struct wl_listener destroy;
};

enum roots_view_type {
	ROOTS_WL_SHELL_VIEW,
	ROOTS_XDG_SHELL_V6_VIEW,
	ROOTS_XWAYLAND_VIEW,
};

struct roots_view {
	struct roots_desktop *desktop;
	double x, y;
	float rotation;
	// TODO: Something for roots-enforced width/height
	enum roots_view_type type;
	union {
		struct wlr_wl_shell_surface *wl_shell_surface;
		struct wlr_xdg_surface_v6 *xdg_surface_v6;
		struct wlr_xwayland_surface *xwayland_surface;
	};
	union {
		struct roots_wl_shell_surface *roots_wl_shell_surface;
		struct roots_xdg_surface_v6 *roots_xdg_surface_v6;
		struct roots_xwayland_surface *roots_xwayland_surface;
	};
	struct wlr_surface *wlr_surface;
	struct wl_list link;
	// TODO: This would probably be better as a field that's updated on a
	// configure event from the xdg_shell
	// If not then this should follow the typical type/impl pattern we use
	// elsewhere
	void (*get_input_bounds)(struct roots_view *view, struct wlr_box *box);
	void (*activate)(struct roots_view *view, bool active);
};

void view_get_input_bounds(struct roots_view *view, struct wlr_box *box);
void view_activate(struct roots_view *view, bool active);

#endif
