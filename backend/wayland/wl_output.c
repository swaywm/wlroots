#include <assert.h>
#include <stdlib.h>
#include <stdint.h>
#include <wayland-client.h>
#include "backend/wayland.h"

static void wl_output_handle_mode(void *data, struct wl_output *wl_output,
		     uint32_t flags, int32_t width, int32_t height, int32_t refresh) {
	struct wlr_wl_output *output = data;
	assert(output->wl_output == wl_output);
	struct wlr_wl_output_mode *mode = calloc(sizeof(struct wlr_wl_output_mode), 1);
	mode->flags = flags;
	mode->width = width;
	mode->height = height;
	mode->refresh = refresh;
	list_add(output->modes, mode);
}

static void wl_output_handle_geometry(void *data, struct wl_output *wl_output,
			 int32_t x, int32_t y, int32_t physical_width, int32_t physical_height,
			 int32_t subpixel, const char *make, const char *model, int32_t transform) {
	struct wlr_wl_output *output = data;
	assert(output->wl_output == wl_output);
	output->x = x;
	output->y = y;
	output->phys_width = physical_width;
	output->phys_height = physical_height;
	output->subpixel = subpixel;
	output->make = make;
	output->model = model;
	output->transform = transform;
}

static void wl_output_handle_scale(void *data, struct wl_output *wl_output, int32_t factor) {
	struct wlr_wl_output *output = data;
	assert(output->wl_output == wl_output);
	output->scale = factor;
}

static void wl_output_handle_done(void *data, struct wl_output *wl_output) {
	// TODO: notify of changes? Probably not necessary for this backend
}

const struct wl_output_listener output_listener = {
	.mode = wl_output_handle_mode,
	.geometry = wl_output_handle_geometry,
	.scale = wl_output_handle_scale,
	.done = wl_output_handle_done
};
