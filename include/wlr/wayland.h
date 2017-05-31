#ifndef _WLR_WAYLAND_H
#define _WLR_WAYLAND_H

#include <wayland-server.h>
#include <wlr/common/list.h>
#include <stdbool.h>

struct wlr_output_mode_state;

struct wlr_output_mode {
	struct wlr_output_mode_state *state;
	uint32_t flags; // enum wl_output_mode
	int32_t width, height;
	int32_t refresh; // mHz
};

struct wlr_output_impl;
struct wlr_output_state;

struct wlr_output {
	const struct wlr_output_impl *impl;
	struct wlr_output_state *state;

	uint32_t flags;
	char *name;
	char *make;
	char *model;
	uint32_t scale;
	int32_t x, y;
	int32_t phys_width, phys_height; // mm
	int32_t subpixel; // enum wl_output_subpixel
	int32_t transform; // enum wl_output_transform

	list_t *modes;
	struct wlr_output_mode *current_mode;

	struct {
		struct wl_signal frame;
	} events;
};

bool wlr_output_set_mode(struct wlr_output *output, struct wlr_output_mode *mode);
void wlr_output_enable(struct wlr_output *output, bool enable);

#endif
