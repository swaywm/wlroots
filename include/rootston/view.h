#ifndef ROOTSTON_VIEW_H
#define ROOTSTON_VIEW_H
#include <stdbool.h>
#include <wlr/config.h>
#include <wlr/types/wlr_box.h>
#include <wlr/types/wlr_foreign_toplevel_management_v1.h>
#include <wlr/types/wlr_surface.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_xdg_shell_v6.h>
#include <wlr/types/wlr_xdg_shell.h>

struct roots_view;

struct roots_view_interface {
	void (*activate)(struct roots_view *view, bool active);
	void (*move)(struct roots_view *view, double x, double y);
	void (*resize)(struct roots_view *view, uint32_t width, uint32_t height);
	void (*move_resize)(struct roots_view *view, double x, double y,
		uint32_t width, uint32_t height);
	void (*maximize)(struct roots_view *view, bool maximized);
	void (*set_fullscreen)(struct roots_view *view, bool fullscreen);
	void (*close)(struct roots_view *view);
	void (*for_each_surface)(struct roots_view *view,
		wlr_surface_iterator_func_t iterator, void *user_data);
	void (*destroy)(struct roots_view *view);
};

enum roots_view_type {
	ROOTS_XDG_SHELL_V6_VIEW,
	ROOTS_XDG_SHELL_VIEW,
#if WLR_HAS_XWAYLAND
	ROOTS_XWAYLAND_VIEW,
#endif
};

struct roots_view {
	enum roots_view_type type;
	const struct roots_view_interface *impl;
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

	struct wlr_surface *wlr_surface;
	struct wl_list children; // roots_view_child::link

	struct wlr_foreign_toplevel_handle_v1 *toplevel_handle;
	struct wl_listener toplevel_handle_request_maximize;
	struct wl_listener toplevel_handle_request_activate;
	struct wl_listener toplevel_handle_request_close;

	struct wl_listener new_subsurface;

	struct {
		struct wl_signal unmap;
		struct wl_signal destroy;
	} events;
};

struct roots_xdg_surface_v6 {
	struct roots_view view;

	struct wlr_xdg_surface_v6 *xdg_surface_v6;

	struct wl_listener destroy;
	struct wl_listener new_popup;
	struct wl_listener map;
	struct wl_listener unmap;
	struct wl_listener request_move;
	struct wl_listener request_resize;
	struct wl_listener request_maximize;
	struct wl_listener request_fullscreen;
	struct wl_listener set_title;
	struct wl_listener set_app_id;

	struct wl_listener surface_commit;

	uint32_t pending_move_resize_configure_serial;
};

struct roots_xdg_toplevel_decoration;

struct roots_xdg_surface {
	struct roots_view view;

	struct wlr_xdg_surface *xdg_surface;

	struct wl_listener destroy;
	struct wl_listener new_popup;
	struct wl_listener map;
	struct wl_listener unmap;
	struct wl_listener request_move;
	struct wl_listener request_resize;
	struct wl_listener request_maximize;
	struct wl_listener request_fullscreen;
	struct wl_listener set_title;
	struct wl_listener set_app_id;

	struct wl_listener surface_commit;

	uint32_t pending_move_resize_configure_serial;

	struct roots_xdg_toplevel_decoration *xdg_toplevel_decoration;
};

#if WLR_HAS_XWAYLAND
struct roots_xwayland_surface {
	struct roots_view view;

	struct wlr_xwayland_surface *xwayland_surface;

	struct wl_listener destroy;
	struct wl_listener request_configure;
	struct wl_listener request_move;
	struct wl_listener request_resize;
	struct wl_listener request_maximize;
	struct wl_listener request_fullscreen;
	struct wl_listener map;
	struct wl_listener unmap;
	struct wl_listener set_title;
	struct wl_listener set_class;

	struct wl_listener surface_commit;
};
#endif

struct roots_view_child;

struct roots_view_child_interface {
	void (*destroy)(struct roots_view_child *child);
};

struct roots_view_child {
	struct roots_view *view;
	const struct roots_view_child_interface *impl;
	struct wlr_surface *wlr_surface;
	struct wl_list link;

	struct wl_listener commit;
	struct wl_listener new_subsurface;
};

struct roots_subsurface {
	struct roots_view_child view_child;
	struct wlr_subsurface *wlr_subsurface;
	struct wl_listener destroy;
	struct wl_listener map;
	struct wl_listener unmap;
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

void view_init(struct roots_view *view, const struct roots_view_interface *impl,
	enum roots_view_type type, struct roots_desktop *desktop);
void view_destroy(struct roots_view *view);
void view_apply_damage(struct roots_view *view);
void view_damage_whole(struct roots_view *view);
void view_update_position(struct roots_view *view, int x, int y);
void view_update_size(struct roots_view *view, int width, int height);
void view_update_decorated(struct roots_view *view, bool decorated);
void view_initial_focus(struct roots_view *view);
void view_map(struct roots_view *view, struct wlr_surface *surface);
void view_unmap(struct roots_view *view);
void view_arrange_maximized(struct roots_view *view);
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
void view_set_title(struct roots_view *view, const char *title);
void view_set_app_id(struct roots_view *view, const char *app_id);
void view_create_foreign_toplevel_handle(struct roots_view *view);
void view_get_deco_box(const struct roots_view *view, struct wlr_box *box);
void view_for_each_surface(struct roots_view *view,
	wlr_surface_iterator_func_t iterator, void *user_data);

struct roots_xdg_surface *roots_xdg_surface_from_view(struct roots_view *view);
struct roots_xdg_surface_v6 *roots_xdg_surface_v6_from_view(
	struct roots_view *view);
struct roots_xwayland_surface *roots_xwayland_surface_from_view(
	struct roots_view *view);

enum roots_deco_part {
	ROOTS_DECO_PART_NONE = 0,
	ROOTS_DECO_PART_TOP_BORDER = (1 << 0),
	ROOTS_DECO_PART_BOTTOM_BORDER = (1 << 1),
	ROOTS_DECO_PART_LEFT_BORDER = (1 << 2),
	ROOTS_DECO_PART_RIGHT_BORDER = (1 << 3),
	ROOTS_DECO_PART_TITLEBAR = (1 << 4),
};

enum roots_deco_part view_get_deco_part(struct roots_view *view, double sx, double sy);

void view_child_init(struct roots_view_child *child,
	const struct roots_view_child_interface *impl, struct roots_view *view,
	struct wlr_surface *wlr_surface);
void view_child_destroy(struct roots_view_child *child);

struct roots_subsurface *subsurface_create(struct roots_view *view,
	struct wlr_subsurface *wlr_subsurface);

#endif
