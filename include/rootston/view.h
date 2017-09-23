#ifndef _ROOTSTON_VIEW_H
#define _ROOTSTON_VIEW_H

struct roots_wl_shell_surface {
	// TODO
	void *_placeholder;
};

struct roots_xdg_surface_v6 {
	// TODO: Maybe destroy listener should go in roots_view
	struct wl_listener destroy_listener;
	struct wl_listener ping_timeout_listener;
	struct wl_listener request_minimize_listener;
	struct wl_listener request_move_listener;
	struct wl_listener request_resize_listener;
	struct wl_listener request_show_window_menu_listener;
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
		struct xdg_shell_v6_surface *xdg_shell_v6_surface;
	};
	union {
		struct roots_wl_shell_surface *roots_wl_shell_surface;
		struct xdg_shell_v6_surface *roots_xdg_surface_v6;
	};
	struct wl_list link;
};

#endif
