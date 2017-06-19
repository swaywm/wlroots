#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-client.h>
#include <wlr/types.h>
#include "types.h"
#include "backend/wayland.h"
#include "common/log.h"

// TODO
static void wlr_wl_output_enable(struct wlr_output_state *output, bool enable) {
}

static bool wlr_wl_output_set_mode(struct wlr_output_state *output,
		struct wlr_output_mode *mode) {
	output->output->current_mode = mode;
	return true;
}

static void wlr_wl_output_transform(struct wlr_output_state *output,
		enum wl_output_transform transform) {
}

static bool wlr_wl_output_set_cursor(struct wlr_output_state *output,
		const uint8_t *buf, int32_t stride, uint32_t width, uint32_t height) {
	return false;
}

static bool wlr_wl_output_move_cursor(struct wlr_output_state *output,
		int x, int y) {
	return false;
}

static void wlr_wl_output_destroy(struct wlr_output_state *output) {
	// TODO: free egl surface
	wl_shell_surface_destroy(output->shell_surface);
	wl_surface_destroy(output->surface);
	free(output);
}

static struct wlr_output_impl output_impl = {
	.enable = wlr_wl_output_enable,
	.set_mode = wlr_wl_output_set_mode,
	.transform = wlr_wl_output_transform,
	.set_cursor = wlr_wl_output_set_cursor,
	.move_cursor = wlr_wl_output_move_cursor,
	.destroy = wlr_wl_output_destroy,
};

struct wlr_output *wlr_wl_output_create(struct wlr_backend_state* backend,
		size_t id) {
	// TODO: dont hardcode stuff like size
	static unsigned int width = 1100;
	static unsigned int height = 720;

	struct wlr_output_state *ostate;
	if (!(ostate = calloc(sizeof(struct wlr_output_state), 1))) {
		wlr_log(L_ERROR, "Failed to allocate wlr_output_state");
		return NULL;
	}

	struct wlr_output *wlr_output = wlr_output_create(&output_impl, ostate);
	if (!wlr_output) {
		free(ostate);
		wlr_log_errno(L_ERROR, "Allocation failed");
		return NULL;
	}

	wlr_output->width = width;
	wlr_output->height = height;
	wlr_output->scale = 1;
	strncpy(wlr_output->make, "wayland-output", sizeof(wlr_output->make));
	strncpy(wlr_output->model, "wayland-output", sizeof(wlr_output->model));
	strncpy(wlr_output->name, "wayland-output", sizeof(wlr_output->name));

	struct wlr_output_mode mode = {
		.width = width,
		.height = height,
		.refresh = 60,
		.flags = 0,
	};
	list_add(wlr_output->modes, &mode);

	ostate->id = id;
	ostate->output = wlr_output;
	ostate->surface = wl_compositor_create_surface(backend->compositor);
	ostate->shell_surface = wl_shell_get_shell_surface(backend->shell, ostate->surface);
	ostate->egl_window = wl_egl_window_create(ostate->surface, width, height);
	ostate->egl_surface = wlr_egl_create_surface(&backend->egl, ostate->egl_window);

	wl_signal_emit(&backend->backend->events.output_add, wlr_output);
	return wlr_output;
}
