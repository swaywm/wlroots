/*
 * This an unstable interface of wlroots. No guarantees are made regarding the
 * future consistency of this API.
 */
#ifndef WLR_USE_UNSTABLE
#error "Add -DWLR_USE_UNSTABLE to enable unstable wlroots features"
#endif

#ifndef WLR_TYPES_WLR_EXT_FOREIGN_TOPLEVEL_MANAGEMENT_V1_H
#define WLR_TYPES_WLR_EXT_FOREIGN_TOPLEVEL_MANAGEMENT_V1_H

#include <wayland-server-core.h>
#include <wlr/types/wlr_ext_foreign_toplevel_info_v1.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_seat.h>

struct wlr_ext_foreign_toplevel_manager_v1 {
	struct wl_global *global;

	struct wl_listener display_destroy;

	struct {
		// wlr_ext_foreign_toplevel_manager_v1_maximize_event
		struct wl_signal request_maximize;
		// wlr_ext_foreign_toplevel_manager_v1_minimize_event
		struct wl_signal request_minimize;
		// wlr_ext_foreign_toplevel_manager_v1_activate_event
		struct wl_signal request_activate;
		// wlr_ext_foreign_toplevel_manager_v1_fullscreen_event
		struct wl_signal request_fullscreen;
		// wlr_ext_foreign_toplevel_manager_v1_close_event
		struct wl_signal request_close;
		// wlr_ext_foreign_toplevel_manager_v1_set_rectangle_event
		struct wl_signal set_rectangle;

		struct wl_signal destroy;
	} events;

	void *data;
};

struct wlr_ext_foreign_toplevel_manager_v1_maximize_event {
	struct wlr_ext_foreign_toplevel_manager_v1 *manager;
	struct wlr_ext_foreign_toplevel_handle_v1 *toplevel;
	bool maximize;
};

struct wlr_ext_foreign_toplevel_manager_v1_minimize_event {
	struct wlr_ext_foreign_toplevel_manager_v1 *manager;
	struct wlr_ext_foreign_toplevel_handle_v1 *toplevel;
	bool minimize;
};

struct wlr_ext_foreign_toplevel_manager_v1_activate_event {
	struct wlr_ext_foreign_toplevel_manager_v1 *manager;
	struct wlr_ext_foreign_toplevel_handle_v1 *toplevel;
	struct wlr_seat *seat;
};

struct wlr_ext_foreign_toplevel_manager_v1_fullscreen_event {
	struct wlr_ext_foreign_toplevel_manager_v1 *manager;
	struct wlr_ext_foreign_toplevel_handle_v1 *toplevel;
	bool fullscreen;
	struct wlr_output *output;
};

struct wlr_ext_foreign_toplevel_manager_v1_close_event {
	struct wlr_ext_foreign_toplevel_manager_v1 *manager;
	struct wlr_ext_foreign_toplevel_handle_v1 *toplevel;
};

struct wlr_ext_foreign_toplevel_manager_v1_set_rectangle_event {
	struct wlr_ext_foreign_toplevel_manager_v1 *manager;
	struct wlr_ext_foreign_toplevel_handle_v1 *toplevel;
	struct wlr_surface *surface;
	int32_t x, y, width, height;
};

struct wlr_ext_foreign_toplevel_manager_v1 *wlr_ext_foreign_toplevel_manager_v1_create(
	struct wl_display *display);

#endif
