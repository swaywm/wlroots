/*
 * This an unstable interface of wlroots. No guarantees are made regarding the
 * future consistency of this API.
 */
#ifndef WLR_USE_UNSTABLE
#error "Add -DWLR_USE_UNSTABLE to enable unstable wlroots features"
#endif

#ifndef WLR_TYPES_WLR_CURSOR_SPY_V1_H
#define WLR_TYPES_WLR_CURSOR_SPY_V1_H

#include <stdbool.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_box.h>

struct wlr_cursor_spy_manager_v1 {
	struct wl_global *global;

	struct wl_listener display_destroy;

	struct {
		struct wl_signal destroy;
	} events;

	void *data;
};

struct wlr_cursor_spy_v1 {
	struct wl_resource *resource;

	enum wl_shm_format format;
	int stride;

	struct wl_shm_buffer *buffer;

	struct wlr_output *output;
	struct wl_listener output_destroy;

	void *data;
};

struct wlr_cursor_spy_manager_v1 *wlr_cursor_spy_manager_v1_create(
	struct wl_display *display);

#endif
