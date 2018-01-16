#ifndef _ROOTSTON_VIEW_H
#define _ROOTSTON_VIEW_H

#include <stdbool.h>
#include <wlr/config.h>
#include <wlr/types/wlr_box.h>
#include <wlr/types/wlr_surface.h>
#include <wlr/types/wlr_xdg_shell_v6.h>

struct roots_wl_shell_surface {
	struct roots_view *view;

	struct wl_listener destroy;
	struct wl_listener request_move;
	struct wl_listener request_resize;
	struct wl_listener request_maximize;
	struct wl_listener request_fullscreen;
	struct wl_listener set_state;

	struct wl_listener surface_commit;
};

struct roots_xdg_surface_v6 {
	struct roots_view *view;

	struct wl_listener commit;
	struct wl_listener destroy;
	struct wl_listener request_move;
	struct wl_listener request_resize;
	struct wl_listener request_maximize;
	struct wl_listener request_fullscreen;

	uint32_t pending_move_resize_configure_serial;
};

struct roots_xwayland_surface {
	struct roots_view *view;

	struct wl_listener destroy;
	struct wl_listener request_configure;
	struct wl_listener request_move;
	struct wl_listener request_resize;
	struct wl_listener request_maximize;
	struct wl_listener request_fullscreen;
	struct wl_listener map_notify;
	struct wl_listener unmap_notify;

	struct wl_listener surface_commit;
};

enum roots_view_type {
	ROOTS_WL_SHELL_VIEW,
	ROOTS_XDG_SHELL_V6_VIEW,
	ROOTS_XWAYLAND_VIEW,
};

struct roots_view {
	struct roots_desktop *desktop;
	struct wl_list link; // roots_desktop::views

	double x, y;
	float rotation;

	bool decorated;
	int border_width;
	int titlebar_height;

	bool maximized;
	struct roots_output *fullscreen_output;
	struct {
		double x, y;
		uint32_t width, height;
		float rotation;
	} saved;

	struct {
		bool update_x, update_y;
		double x, y;
		uint32_t width, height;
	} pending_move_resize;

	// TODO: Something for roots-enforced width/height
	enum roots_view_type type;
	union {
		struct wlr_wl_shell_surface *wl_shell_surface;
		struct wlr_xdg_surface_v6 *xdg_surface_v6;
#ifdef WLR_HAS_XWAYLAND
		struct wlr_xwayland_surface *xwayland_surface;
#endif
	};
	union {
		struct roots_wl_shell_surface *roots_wl_shell_surface;
		struct roots_xdg_surface_v6 *roots_xdg_surface_v6;
#ifdef WLR_HAS_XWAYLAND
		struct roots_xwayland_surface *roots_xwayland_surface;
#endif
	};
	struct wlr_surface *wlr_surface;

	struct {
		struct wl_signal destroy;
	} events;

	// TODO: This would probably be better as a field that's updated on a
	// configure event from the xdg_shell
	// If not then this should follow the typical type/impl pattern we use
	// elsewhere
	void (*get_size)(const struct roots_view *view, struct wlr_box *box);
	void (*activate)(struct roots_view *view, bool active);
	void (*move)(struct roots_view *view, double x, double y);
	void (*resize)(struct roots_view *view, uint32_t width, uint32_t height);
	void (*move_resize)(struct roots_view *view, double x, double y,
		uint32_t width, uint32_t height);
	void (*maximize)(struct roots_view *view, bool maximized);
	void (*set_fullscreen)(struct roots_view *view, bool fullscreen);
	void (*close)(struct roots_view *view);
};

void view_get_box(const struct roots_view *view, struct wlr_box *box);
void view_get_deco_box(const struct roots_view *view, struct wlr_box *box);
void view_activate(struct roots_view *view, bool active);
void view_move(struct roots_view *view, double x, double y);
void view_resize(struct roots_view *view, uint32_t width, uint32_t height);
void view_move_resize(struct roots_view *view, double x, double y,
	uint32_t width, uint32_t height);
void view_maximize(struct roots_view *view, bool maximized);
void view_set_fullscreen(struct roots_view *view, bool fullscreen,
	struct wlr_output *output);
void view_close(struct roots_view *view);
bool view_center(struct roots_view *view);
void view_setup(struct roots_view *view);
void view_teardown(struct roots_view *view);

#endif
