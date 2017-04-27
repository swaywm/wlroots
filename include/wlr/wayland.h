#ifndef _WLR_WAYLAND_H
#define _WLR_WAYLAND_H

#include <wayland-server.h>
#include <wlr/common/list.h>

struct wlr_wl_seat {
	struct wl_seat *wl_seat;
	uint32_t capabilities;
	char *name;
	list_t *keyboards;
	list_t *pointers;
};

void wlr_wl_seat_free(struct wlr_wl_seat *seat);

struct wlr_wl_output_mode {
	uint32_t flags; // enum wl_output_mode
	int32_t width, height;
	int32_t refresh; // mHz
};

struct wlr_wl_output {
	struct wl_output *wl_output;
	uint32_t flags;
	char *make;
	char *model;
	uint32_t scale;
	int32_t x, y;
	int32_t phys_width, phys_height; // mm
	int32_t subpixel; // enum wl_output_subpixel
	int32_t transform; // enum wl_output_transform
	list_t *modes;
	struct wlr_wl_output_mode *current_mode;
};

void wlr_wl_output_free(struct wlr_wl_output *output);

struct wlr_wl_keyboard {
	struct wl_keyboard *wl_keyboard;
};

struct wlr_wl_pointer {
	struct wl_pointer *wl_pointer;
	//struct wl_surface *current_surface;
	struct wl_client *current_client;
	wl_fixed_t x, y;
};

#endif
