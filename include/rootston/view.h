#ifndef _ROOTSTON_VIEW_H
#define _ROOTSTON_VIEW_H
#include <stdbool.h>
#include <wlr/types/wlr_xdg_shell_v6.h>
#include <wlr/types/wlr_surface.h>

struct roots_wl_shell_surface {
	// TODO
	void *_placeholder;
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

enum roots_view_type {
	ROOTS_WL_SHELL_VIEW,
	ROOTS_XDG_SHELL_V6_VIEW,
	ROOTS_XWAYLAND_VIEW,
};

struct roots_view {
	double x, y;
	float rotation;
	// TODO: Something for roots-enforced width/height
	enum roots_view_type type;
	union {
		struct wlr_shell_surface *wl_shell_surface;
		struct wlr_xdg_surface_v6 *xdg_surface_v6;
	};
	union {
		struct roots_wl_shell_surface *roots_wl_shell_surface;
		struct roots_xdg_surface_v6 *roots_xdg_surface_v6;
	};
	struct wlr_surface *wlr_surface;
	struct wl_list link;
};

#endif
