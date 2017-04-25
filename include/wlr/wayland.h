#ifndef _WLR_WAYLAND_H
#define _WLR_WAYLAND_H

#include <wayland-server.h>
#include <wlr/common/list.h>

struct wlr_wl_seat {
	struct wl_seat *wl_seat;
	uint32_t capabilities;
	const char *name;
	list_t *outputs;
	list_t *pointers;
};

struct wlr_wl_output {
	struct wl_output *wl_output;
	uint32_t flags;
	uint32_t width, height;
	uint32_t scale;
};

struct wlr_wl_keyboard {
	struct wl_keyboard *wl_keyboard;
};

struct wlr_wl_pointer {
	struct wl_pointer *wl_pointer;
	struct wl_surface *current_surface;
	wl_fixed_t x, y;
};

#endif
