#ifndef ROOTSTON_VIEW_H
#define ROOTSTON_VIEW_H
#include <stdbool.h>
#include <wlr/config.h>
#include <wlr/types/wlr_box.h>
#include <wlr/types/wlr_surface.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_xdg_shell_v6.h>
#include <wlr/types/wlr_xdg_shell.h>

struct roots_wl_shell_surface {
	struct roots_view *view;

	struct wl_listener destroy;
	struct wl_listener new_popup;
	struct wl_listener request_move;
	struct wl_listener request_resize;
	struct wl_listener request_maximize;
	struct wl_listener request_fullscreen;
	struct wl_listener set_state;

	struct wl_listener surface_commit;
};

struct roots_xdg_surface_v6 {
	struct roots_view *view;

	struct wl_listener destroy;
	struct wl_listener new_popup;
	struct wl_listener map;
	struct wl_listener unmap;
	struct wl_listener request_move;
	struct wl_listener request_resize;
	struct wl_listener request_maximize;
	struct wl_listener request_fullscreen;

	struct wl_listener surface_commit;

	uint32_t pending_move_resize_configure_serial;
};

struct roots_xdg_toplevel_decoration;

struct roots_xdg_surface {
	struct roots_view *view;

	struct wl_listener destroy;
	struct wl_listener new_popup;
	struct wl_listener map;
	struct wl_listener unmap;
	struct wl_listener request_move;
	struct wl_listener request_resize;
	struct wl_listener request_maximize;
	struct wl_listener request_fullscreen;

	struct wl_listener surface_commit;

	uint32_t pending_move_resize_configure_serial;

	struct roots_xdg_toplevel_decoration *xdg_toplevel_decoration;
};

struct roots_xwayland_surface {
	struct roots_view *view;

	struct wl_listener destroy;
	struct wl_listener request_configure;
	struct wl_listener request_move;
	struct wl_listener request_resize;
	struct wl_listener request_maximize;
	struct wl_listener request_fullscreen;
	struct wl_listener map;
	struct wl_listener unmap;

	struct wl_listener surface_commit;
};

enum roots_view_type {
	ROOTS_WL_SHELL_VIEW,
	ROOTS_XDG_SHELL_V6_VIEW,
	ROOTS_XDG_SHELL_VIEW,
#if WLR_HAS_XWAYLAND
	ROOTS_XWAYLAND_VIEW,
#endif
};

struct roots_view {
	struct roots_desktop *desktop;
	struct wl_list link; // roots_desktop::views

	struct wlr_box box;
	float rotation;
	float alpha;

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
		struct wlr_xdg_surface *xdg_surface;
#if WLR_HAS_XWAYLAND
		struct wlr_xwayland_surface *xwayland_surface;
#endif
	};
	union {
		struct roots_wl_shell_surface *roots_wl_shell_surface;
		struct roots_xdg_surface_v6 *roots_xdg_surface_v6;
		struct roots_xdg_surface *roots_xdg_surface;
#if WLR_HAS_XWAYLAND
		struct roots_xwayland_surface *roots_xwayland_surface;
#endif
	};

	struct wlr_surface *wlr_surface;
	struct wl_list children; // roots_view_child::link

	struct wl_listener new_subsurface;

	struct {
		struct wl_signal unmap;
		struct wl_signal destroy;
	} events;

	// TODO: this should follow the typical type/impl pattern we use elsewhere
	void (*activate)(struct roots_view *view, bool active);
	void (*move)(struct roots_view *view, double x, double y);
	void (*resize)(struct roots_view *view, uint32_t width, uint32_t height);
	void (*move_resize)(struct roots_view *view, double x, double y,
		uint32_t width, uint32_t height);
	void (*maximize)(struct roots_view *view, bool maximized);
	void (*set_fullscreen)(struct roots_view *view, bool fullscreen);
	void (*close)(struct roots_view *view);
	void (*destroy)(struct roots_view *view);
};

struct roots_view_child {
	struct roots_view *view;
	struct wlr_surface *wlr_surface;
	struct wl_list link;

	struct wl_listener commit;
	struct wl_listener new_subsurface;

	void (*destroy)(struct roots_view_child *child);
};

struct roots_subsurface {
	struct roots_view_child view_child;
	struct wlr_subsurface *wlr_subsurface;
	struct wl_listener destroy;
};

struct roots_wl_shell_popup {
	struct roots_view_child view_child;
	struct wlr_wl_shell_surface *wlr_wl_shell_surface;
	struct wl_listener destroy;
	struct wl_listener set_state;
	struct wl_listener new_popup;
};

struct roots_xdg_popup_v6 {
	struct roots_view_child view_child;
	struct wlr_xdg_popup_v6 *wlr_popup;
	struct wl_listener destroy;
	struct wl_listener map;
	struct wl_listener unmap;
	struct wl_listener new_popup;
};

struct roots_xdg_popup {
	struct roots_view_child view_child;
	struct wlr_xdg_popup *wlr_popup;
	struct wl_listener destroy;
	struct wl_listener map;
	struct wl_listener unmap;
	struct wl_listener new_popup;
};

struct roots_xdg_toplevel_decoration {
	struct wlr_xdg_toplevel_decoration_v1 *wlr_decoration;
	struct roots_xdg_surface *surface;
	struct wl_listener destroy;
	struct wl_listener request_mode;
	struct wl_listener surface_commit;
};

void view_get_box(const struct roots_view *view, struct wlr_box *box);
void view_activate(struct roots_view *view, bool active);
void view_move(struct roots_view *view, double x, double y);
void view_resize(struct roots_view *view, uint32_t width, uint32_t height);
void view_move_resize(struct roots_view *view, double x, double y,
	uint32_t width, uint32_t height);
void view_maximize(struct roots_view *view, bool maximized);
void view_set_fullscreen(struct roots_view *view, bool fullscreen,
	struct wlr_output *output);
void view_rotate(struct roots_view *view, float rotation);
void view_cycle_alpha(struct roots_view *view);
void view_close(struct roots_view *view);
bool view_center(struct roots_view *view);
void view_setup(struct roots_view *view);
void view_teardown(struct roots_view *view);

void view_get_deco_box(const struct roots_view *view, struct wlr_box *box);

enum roots_deco_part {
	ROOTS_DECO_PART_NONE = 0,
	ROOTS_DECO_PART_TOP_BORDER = (1 << 0),
	ROOTS_DECO_PART_BOTTOM_BORDER = (1 << 1),
	ROOTS_DECO_PART_LEFT_BORDER = (1 << 2),
	ROOTS_DECO_PART_RIGHT_BORDER = (1 << 3),
	ROOTS_DECO_PART_TITLEBAR = (1 << 4),
};

enum roots_deco_part view_get_deco_part(struct roots_view *view, double sx, double sy);

void view_child_init(struct roots_view_child *child, struct roots_view *view,
	struct wlr_surface *wlr_surface);
void view_child_finish(struct roots_view_child *child);

struct roots_subsurface *subsurface_create(struct roots_view *view,
	struct wlr_subsurface *wlr_subsurface);

#endif
