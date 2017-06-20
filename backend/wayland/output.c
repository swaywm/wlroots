#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-client.h>
#include <wlr/types.h>
#include <GLES3/gl3.h>
#include "types.h"
#include "backend/wayland.h"
#include "common/log.h"

static struct wl_callback_listener frame_listener;

static void surface_frame_callback(void *data, struct wl_callback *cb, uint32_t time) {
	struct wlr_output_state *output = data;
	assert(output);

	if (!eglMakeCurrent(output->backend->egl.display,
		output->egl_surface, output->egl_surface,
		output->backend->egl.context)) {
		wlr_log(L_ERROR, "eglMakeCurrent failed: %s", egl_error());
		return;
	}

	wl_signal_emit(&output->output->events.frame, output->output);
	output->frame_callback = wl_surface_frame(output->surface);
	wl_callback_add_listener(output->frame_callback, &frame_listener, output);
	wl_callback_destroy(cb);

	if (!eglSwapBuffers(output->backend->egl.display, output->egl_surface)) {
		wlr_log(L_ERROR, "eglSwapBuffers failed: %s", egl_error());
		return;
	}
}

static struct wl_callback_listener frame_listener = {
	.done = surface_frame_callback
};

// TODO: enable/cursor etc
static void wlr_wl_output_enable(struct wlr_output_state *output, bool enable) {
}

static bool wlr_wl_output_set_mode(struct wlr_output_state *output,
		struct wlr_output_mode *mode) {
	output->output->current_mode = mode;

	// start rendering loop per callbacks by rendering first frame
	if (!eglMakeCurrent(output->backend->egl.display,
		output->egl_surface, output->egl_surface,
		output->backend->egl.context)) {
		wlr_log(L_ERROR, "eglMakeCurrent failed: %s", egl_error());
		return false;
	}

	glViewport(0, 0, output->width, output->height);
	glClearColor(1.0, 1.0, 1.0, 1.0);
	glClear(GL_COLOR_BUFFER_BIT);

	output->frame_callback = wl_surface_frame(output->surface);
	wl_callback_add_listener(output->frame_callback, &frame_listener, output);

	if (!eglSwapBuffers(output->backend->egl.display, output->egl_surface)) {
		wlr_log(L_ERROR, "eglSwapBuffers failed: %s", egl_error());
		return false;
	}

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

void handle_ping(void* data, struct wl_shell_surface* ssurface, uint32_t serial) {
	struct wlr_output_state *output = data;
	assert(output && output->shell_surface == ssurface);
	wl_shell_surface_pong(ssurface, serial);
}

void handle_configure(void *data, struct wl_shell_surface *wl_shell_surface,
		uint32_t edges, int32_t width, int32_t height){
	wlr_log(L_DEBUG, "resize %d %d", width, height);
}

void handle_popup_done(void *data, struct wl_shell_surface *wl_shell_surface) {
	wlr_log(L_ERROR, "Unexpected call");
}

static struct wl_shell_surface_listener shell_surface_listener = {
	.ping = handle_ping,
	.configure = handle_configure,
	.popup_done = handle_popup_done
};

struct wlr_output *wlr_wl_output_create(struct wlr_backend_state* backend,
		size_t id) {
	// TODO: dont hardcode stuff like size
	static unsigned int width = 800;
	static unsigned int height = 500;

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
	ostate->width = width;
	ostate->height = height;
	ostate->backend = backend;
	ostate->output = wlr_output;

	// TODO: error handling
	ostate->surface = wl_compositor_create_surface(backend->compositor);
	ostate->shell_surface = wl_shell_get_shell_surface(backend->shell, ostate->surface);

	wl_shell_surface_set_class(ostate->shell_surface, "sway");
	wl_shell_surface_set_title(ostate->shell_surface, "sway-wl");
	wl_shell_surface_add_listener(ostate->shell_surface, &shell_surface_listener, ostate);

	ostate->egl_window = wl_egl_window_create(ostate->surface, width, height);
	ostate->egl_surface = wlr_egl_create_surface(&backend->egl, ostate->egl_window);

	wl_signal_emit(&backend->backend->events.output_add, wlr_output);
	return wlr_output;
}
