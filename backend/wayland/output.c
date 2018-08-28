#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>
#include <wayland-client.h>
#include <wlr/interfaces/wlr_output.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_matrix.h>
#include <wlr/util/log.h>
#include "backend/wayland.h"
#include "util/signal.h"
#include "xdg-shell-unstable-v6-client-protocol.h"

static struct wlr_wl_output *get_wl_output_from_output(
		struct wlr_output *wlr_output) {
	assert(wlr_output_is_wl(wlr_output));
	return (struct wlr_wl_output *)wlr_output;
}

static struct wl_callback_listener frame_listener;

static void surface_frame_callback(void *data, struct wl_callback *cb,
		uint32_t time) {
	struct wlr_wl_output *output = data;
	assert(output);
	wl_callback_destroy(cb);
	output->frame_callback = NULL;

	wlr_output_send_frame(&output->wlr_output);
}

static struct wl_callback_listener frame_listener = {
	.done = surface_frame_callback
};

static bool output_set_custom_mode(struct wlr_output *wlr_output,
		int32_t width, int32_t height, int32_t refresh) {
	struct wlr_wl_output *output = get_wl_output_from_output(wlr_output);
	wlr_render_surface_resize(output->render_surface, width, height);
	wlr_output_update_custom_mode(&output->wlr_output, width, height, 0);
	return true;
}

static bool output_swap_buffers(struct wlr_output *wlr_output,
		pixman_region32_t *damage) {
	struct wlr_wl_output *output =
		get_wl_output_from_output(wlr_output);

	if (output->frame_callback != NULL) {
		wlr_log(WLR_ERROR, "Skipping buffer swap");
		return false;
	}

	output->frame_callback = wl_surface_frame(output->surface);
	wl_callback_add_listener(output->frame_callback, &frame_listener, output);

	if (!wlr_render_surface_swap_buffers(output->render_surface, damage)) {
		return false;
	}

	// TODO: if available, use the presentation-time protocol
	wlr_output_send_present(wlr_output, NULL);
	return true;
}

static void output_transform(struct wlr_output *wlr_output,
		enum wl_output_transform transform) {
	struct wlr_wl_output *output = get_wl_output_from_output(wlr_output);
	output->wlr_output.transform = transform;
}

static bool output_set_cursor(struct wlr_output *wlr_output,
		struct wlr_texture *texture, int32_t scale,
		enum wl_output_transform transform,
		int32_t hotspot_x, int32_t hotspot_y, bool update_texture) {
	struct wlr_wl_output *output = get_wl_output_from_output(wlr_output);
	struct wlr_wl_backend *backend = output->backend;

	struct wlr_box hotspot = { .x = hotspot_x, .y = hotspot_y };
	wlr_box_transform(&hotspot,
		wlr_output_transform_invert(wlr_output->transform),
		output->cursor.width, output->cursor.height, &hotspot);

	// TODO: use output->wlr_output.transform to transform pixels and hotpot
	output->cursor.hotspot_x = hotspot.x;
	output->cursor.hotspot_y = hotspot.y;

	if (!update_texture) {
		// Update hotspot without changing cursor image
		update_wl_output_cursor(output);
		return true;
	}

	if (output->cursor.surface == NULL) {
		output->cursor.surface =
			wl_compositor_create_surface(backend->compositor);
	}
	struct wl_surface *surface = output->cursor.surface;

	if (texture != NULL) {
		int width, height;
		wlr_texture_get_size(texture, &width, &height);
		width = width * wlr_output->scale / scale;
		height = height * wlr_output->scale / scale;

		output->cursor.width = width;
		output->cursor.height = height;

		if (output->cursor.render_surface == NULL) {
			output->cursor.render_surface = wlr_renderer_create_render_surface(
				backend->renderer, surface, width, height);
		} else {
			wlr_render_surface_resize(output->cursor.render_surface,
				width, height);
		}

		struct wlr_box cursor_box = {
			.width = width,
			.height = height,
		};

		float projection[9];
		wlr_matrix_projection(projection, width, height, wlr_output->transform);

		float matrix[9];
		wlr_matrix_project_box(matrix, &cursor_box, transform, 0, projection);

		wlr_renderer_begin(backend->renderer, output->cursor.render_surface);
		wlr_renderer_clear(backend->renderer, (float[]){ 0.0, 0.0, 0.0, 0.0 });
		wlr_render_texture_with_matrix(backend->renderer, texture, matrix, 1.0);
		wlr_renderer_end(backend->renderer);

		wlr_render_surface_swap_buffers(output->cursor.render_surface, NULL);
	} else {
		wl_surface_attach(surface, NULL, 0, 0);
		wl_surface_commit(surface);
	}

	update_wl_output_cursor(output);
	return true;
}

static void output_destroy(struct wlr_output *wlr_output) {
	struct wlr_wl_output *output = get_wl_output_from_output(wlr_output);
	if (output == NULL) {
		return;
	}

	wl_list_remove(&output->link);

	wlr_render_surface_destroy(output->cursor.render_surface);
	if (output->cursor.surface) {
		wl_surface_destroy(output->cursor.surface);
	}

	if (output->frame_callback) {
		wl_callback_destroy(output->frame_callback);
	}

	wlr_render_surface_destroy(output->render_surface);
	zxdg_toplevel_v6_destroy(output->xdg_toplevel);
	zxdg_surface_v6_destroy(output->xdg_surface);
	wl_surface_destroy(output->surface);
	free(output);
}

void update_wl_output_cursor(struct wlr_wl_output *output) {
	if (output->backend->pointer && output->enter_serial) {
		wl_pointer_set_cursor(output->backend->pointer, output->enter_serial,
			output->cursor.surface, output->cursor.hotspot_x,
			output->cursor.hotspot_y);
	}
}

static bool output_move_cursor(struct wlr_output *_output, int x, int y) {
	// TODO: only return true if x == current x and y == current y
	return true;
}

static void output_schedule_frame(struct wlr_output *wlr_output) {
	struct wlr_wl_output *output = get_wl_output_from_output(wlr_output);

	if (output->frame_callback != NULL) {
		wlr_log(WLR_ERROR, "Skipping frame scheduling");
		return;
	}

	output->frame_callback = wl_surface_frame(output->surface);
	wl_callback_add_listener(output->frame_callback, &frame_listener, output);
	wl_surface_commit(output->surface);
}

static struct wlr_render_surface *output_get_render_surface(
		struct wlr_output *wlr_output) {
	struct wlr_wl_output *output = (struct wlr_wl_output *)wlr_output;
	return output->render_surface;
}

static const struct wlr_output_impl output_impl = {
	.set_custom_mode = output_set_custom_mode,
	.transform = output_transform,
	.destroy = output_destroy,
	.swap_buffers = output_swap_buffers,
	.set_cursor = output_set_cursor,
	.move_cursor = output_move_cursor,
	.schedule_frame = output_schedule_frame,
	.get_render_surface = output_get_render_surface
};

bool wlr_output_is_wl(struct wlr_output *wlr_output) {
	return wlr_output->impl == &output_impl;
}

static void xdg_surface_handle_configure(void *data, struct zxdg_surface_v6 *xdg_surface,
		uint32_t serial) {
	struct wlr_wl_output *output = data;
	assert(output && output->xdg_surface == xdg_surface);

	zxdg_surface_v6_ack_configure(xdg_surface, serial);

	// nothing else?
}

static struct zxdg_surface_v6_listener xdg_surface_listener = {
	.configure = xdg_surface_handle_configure,
};

static void xdg_toplevel_handle_configure(void *data,
		struct zxdg_toplevel_v6 *xdg_toplevel,
		int32_t width, int32_t height, struct wl_array *states) {
	struct wlr_wl_output *output = data;
	assert(output && output->xdg_toplevel == xdg_toplevel);

	if (width == 0 || height == 0) {
		return;
	}
	// loop over states for maximized etc?
	wlr_render_surface_resize(output->render_surface, width, height);
	wlr_output_update_custom_mode(&output->wlr_output, width, height, 0);
}

static void xdg_toplevel_handle_close(void *data,
		struct zxdg_toplevel_v6 *xdg_toplevel) {
	struct wlr_wl_output *output = data;
	assert(output && output->xdg_toplevel == xdg_toplevel);

	wlr_output_destroy((struct wlr_output *)output);
}

static struct zxdg_toplevel_v6_listener xdg_toplevel_listener = {
	.configure = xdg_toplevel_handle_configure,
	.close = xdg_toplevel_handle_close,
};

struct wlr_output *wlr_wl_output_create(struct wlr_backend *wlr_backend) {
	struct wlr_wl_backend *backend = get_wl_backend_from_backend(wlr_backend);
	if (!backend->started) {
		++backend->requested_outputs;
		return NULL;
	}

	struct wlr_wl_output *output;
	if (!(output = calloc(sizeof(struct wlr_wl_output), 1))) {
		wlr_log(WLR_ERROR, "Failed to allocate wlr_wl_output");
		return NULL;
	}
	wlr_output_init(&output->wlr_output, &backend->backend, &output_impl,
		backend->local_display);
	struct wlr_output *wlr_output = &output->wlr_output;

	wlr_output_update_custom_mode(wlr_output, 1280, 720, 0);
	strncpy(wlr_output->make, "wayland", sizeof(wlr_output->make));
	strncpy(wlr_output->model, "wayland", sizeof(wlr_output->model));
	snprintf(wlr_output->name, sizeof(wlr_output->name), "WL-%d",
		wl_list_length(&backend->outputs) + 1);

	output->backend = backend;

	output->surface = wl_compositor_create_surface(backend->compositor);
	if (!output->surface) {
		wlr_log_errno(WLR_ERROR, "Could not create output surface");
		goto error;
	}
	wl_surface_set_user_data(output->surface, output);
	output->xdg_surface =
		zxdg_shell_v6_get_xdg_surface(backend->shell, output->surface);
	if (!output->xdg_surface) {
		wlr_log_errno(WLR_ERROR, "Could not get xdg surface");
		goto error;
	}
	output->xdg_toplevel =
		zxdg_surface_v6_get_toplevel(output->xdg_surface);
	if (!output->xdg_toplevel) {
		wlr_log_errno(WLR_ERROR, "Could not get xdg toplevel");
		goto error;
	}

	char title[32];
	if (snprintf(title, sizeof(title), "wlroots - %s", wlr_output->name)) {
		zxdg_toplevel_v6_set_title(output->xdg_toplevel, title);
	}

	zxdg_toplevel_v6_set_app_id(output->xdg_toplevel, "wlroots");
	zxdg_surface_v6_add_listener(output->xdg_surface,
			&xdg_surface_listener, output);
	zxdg_toplevel_v6_add_listener(output->xdg_toplevel,
			&xdg_toplevel_listener, output);
	wl_surface_commit(output->surface);

	output->render_surface = wlr_renderer_create_render_surface(
		backend->renderer, output->surface, wlr_output->width,
		wlr_output->height);

	wl_display_roundtrip(output->backend->remote_display);

	wlr_renderer_begin(backend->renderer, output->render_surface);
	wlr_renderer_clear(backend->renderer, (float[]){ 1.0, 1.0, 1.0, 1.0 });
	wlr_renderer_end(backend->renderer);

	output->frame_callback = wl_surface_frame(output->surface);
	wl_callback_add_listener(output->frame_callback, &frame_listener, output);

	if (!wlr_render_surface_swap_buffers(output->render_surface, NULL)) {
		goto error;
	}

	wl_list_insert(&backend->outputs, &output->link);
	wlr_output_update_enabled(wlr_output, true);

	wlr_signal_emit_safe(&backend->backend.events.new_output, wlr_output);

	if (backend->pointer != NULL) {
		create_wl_pointer(backend->pointer, output);
	}

	return wlr_output;

error:
	wlr_output_destroy(&output->wlr_output);
	return NULL;
}
