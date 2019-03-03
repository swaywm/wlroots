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
#include <wlr/backend/interface.h>

#include "backend/wayland.h"
#include "util/signal.h"
#include "xdg-shell-client-protocol.h"

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
#if 0
	wlr_egl_swap_buffers(&output->backend->egl, output->egl_surface, NULL);
	wl_egl_window_resize(output->egl_window, width, height, 0, 0);
#endif
	wlr_output_update_custom_mode(&output->wlr_output, width, height, 0);

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
#if 0
	struct wlr_wl_output *output = get_wl_output_from_output(wlr_output);
	struct wlr_wl_backend *backend = output->backend;

	struct wlr_box hotspot = { .x = hotspot_x, .y = hotspot_y };
	wlr_box_transform(&hotspot, &hotspot,
		wlr_output_transform_invert(wlr_output->transform),
		output->cursor.width, output->cursor.height);

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

		if (output->cursor.egl_window == NULL) {
			output->cursor.egl_window =
				wl_egl_window_create(surface, width, height);
		}
		wl_egl_window_resize(output->cursor.egl_window, width, height, 0, 0);

		EGLSurface egl_surface =
			wlr_egl_create_surface(&backend->egl, output->cursor.egl_window);

		wlr_egl_make_current(&backend->egl, egl_surface, NULL);

		struct wlr_box cursor_box = {
			.width = width,
			.height = height,
		};

		float projection[9];
		wlr_matrix_projection(projection, width, height, wlr_output->transform);

		float matrix[9];
		wlr_matrix_project_box(matrix, &cursor_box, transform, 0, projection);

		wlr_renderer_begin(backend->renderer, width, height);
		wlr_renderer_clear(backend->renderer, (float[]){ 0.0, 0.0, 0.0, 0.0 });
		wlr_render_texture_with_matrix(backend->renderer, texture, matrix, 1.0);
		wlr_renderer_end(backend->renderer);

		wlr_egl_swap_buffers(&backend->egl, egl_surface, NULL);
		wlr_egl_destroy_surface(&backend->egl, egl_surface);
	} else {
		wl_surface_attach(surface, NULL, 0, 0);
		wl_surface_commit(surface);
	}

	update_wl_output_cursor(output);
#endif
	return true;
}

static void output_destroy(struct wlr_output *wlr_output) {
	struct wlr_wl_output *output = get_wl_output_from_output(wlr_output);
	if (output == NULL) {
		return;
	}

	wl_list_remove(&output->link);

	//if (output->cursor.egl_window != NULL) {
	//	wl_egl_window_destroy(output->cursor.egl_window);
	//}
	if (output->cursor.surface) {
		wl_surface_destroy(output->cursor.surface);
	}

	if (output->frame_callback) {
		wl_callback_destroy(output->frame_callback);
	}

	//wlr_egl_destroy_surface(&output->backend->egl, output->egl_surface);
	//wl_egl_window_destroy(output->egl_window);
	xdg_toplevel_destroy(output->xdg_toplevel);
	xdg_surface_destroy(output->xdg_surface);
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

static bool output_schedule_frame(struct wlr_output *wlr_output) {
	struct wlr_wl_output *output = get_wl_output_from_output(wlr_output);

	if (output->frame_callback != NULL) {
		wlr_log(WLR_ERROR, "Skipping frame scheduling");
		return true;
	}

	output->frame_callback = wl_surface_frame(output->surface);
	wl_callback_add_listener(output->frame_callback, &frame_listener, output);
	wl_surface_commit(output->surface);
	return true;
}

static const struct wlr_format_set *output_get_formats(struct wlr_output *wlr_output) {
	struct wlr_wl_output *output = get_wl_output_from_output(wlr_output);

	return &output->backend->formats;
}

static bool output_present(struct wlr_output *wlr_output) {
	struct wlr_wl_output *output = get_wl_output_from_output(wlr_output);
	struct wl_buffer *buf = NULL;

	if (wlr_output->image) {
		buf = wlr_output->image->backend_priv;
	}

	if (!output->frame_callback) {
		output->frame_callback = wl_surface_frame(output->surface);
		wl_callback_add_listener(output->frame_callback, &frame_listener,
			output);
	}

	wl_surface_attach(output->surface, buf, 0, 0);

	int n_rects;
	pixman_box32_t *rects = pixman_region32_rectangles(&wlr_output->damage_2, &n_rects);
	for (int i = 0; i < n_rects; ++i) {
		wl_surface_damage_buffer(output->surface, rects[i].x1, rects[i].y1,
			rects[i].x2 - rects[i].x1, rects[i].y2 - rects[i].y1);
	}

	wl_surface_commit(output->surface);

	return true;
}

static const struct wlr_output_impl output_impl = {
	.set_custom_mode = output_set_custom_mode,
	.transform = output_transform,
	.destroy = output_destroy,
	.set_cursor = output_set_cursor,
	.move_cursor = output_move_cursor,
	.schedule_frame = output_schedule_frame,
	.get_formats = output_get_formats,
	.present = output_present,
};

bool wlr_output_is_wl(struct wlr_output *wlr_output) {
	return wlr_output->impl == &output_impl;
}

static void xdg_surface_handle_configure(void *data,
		struct xdg_surface *xdg_surface, uint32_t serial) {
	struct wlr_wl_output *output = data;
	assert(output && output->xdg_surface == xdg_surface);

	xdg_surface_ack_configure(xdg_surface, serial);

	// nothing else?
}

static struct xdg_surface_listener xdg_surface_listener = {
	.configure = xdg_surface_handle_configure,
};

static void xdg_toplevel_handle_configure(void *data,
		struct xdg_toplevel *xdg_toplevel,
		int32_t width, int32_t height, struct wl_array *states) {
	struct wlr_wl_output *output = data;
	assert(output && output->xdg_toplevel == xdg_toplevel);

	if (width == 0 || height == 0) {
		return;
	}
	// loop over states for maximized etc?
	output_set_custom_mode(&output->wlr_output, width, height, 0);
}

static void xdg_toplevel_handle_close(void *data,
		struct xdg_toplevel *xdg_toplevel) {
	struct wlr_wl_output *output = data;
	assert(output && output->xdg_toplevel == xdg_toplevel);

	wlr_output_destroy((struct wlr_output *)output);
}

static struct xdg_toplevel_listener xdg_toplevel_listener = {
	.configure = xdg_toplevel_handle_configure,
	.close = xdg_toplevel_handle_close,
};

struct wlr_output *wlr_wl_output_create(struct wlr_backend *backend) {
	struct wlr_wl_backend *wl = get_wl_backend_from_backend(backend);
	if (!wl->started) {
		++wl->requested_outputs;
		return NULL;
	}

	struct wlr_wl_output *output = calloc(1, sizeof(*output));
	if (!output) {
		wlr_log(WLR_ERROR, "Allocation failed");
		return NULL;
	}

	wlr_output_init(&output->wlr_output, &wl->backend, &output_impl,
		wl->local_display);
	struct wlr_output *wlr_output = &output->wlr_output;

	strncpy(wlr_output->make, "wayland", sizeof(wlr_output->make));
	strncpy(wlr_output->model, "wayland", sizeof(wlr_output->model));
	snprintf(wlr_output->name, sizeof(wlr_output->name), "WL-%d",
		wl_list_length(&wl->outputs) + 1);

	output->backend = wl;

	output_set_custom_mode(&output->wlr_output, 1280, 720, 0);

	output->surface = wl_compositor_create_surface(wl->compositor);
	if (!output->surface) {
		wlr_log_errno(WLR_ERROR, "Could not create output surface");
		goto error;
	}
	wl_surface_set_user_data(output->surface, output);
	output->xdg_surface =
		xdg_wm_base_get_xdg_surface(wl->xdg_wm_base, output->surface);
	if (!output->xdg_surface) {
		wlr_log_errno(WLR_ERROR, "Could not get xdg surface");
		goto error;
	}
	output->xdg_toplevel =
		xdg_surface_get_toplevel(output->xdg_surface);
	if (!output->xdg_toplevel) {
		wlr_log_errno(WLR_ERROR, "Could not get xdg toplevel");
		goto error;
	}

	wlr_wl_output_set_title(wlr_output, NULL);

	xdg_toplevel_set_app_id(output->xdg_toplevel, "wlroots");
	xdg_surface_add_listener(output->xdg_surface,
			&xdg_surface_listener, output);
	xdg_toplevel_add_listener(output->xdg_toplevel,
			&xdg_toplevel_listener, output);
	wl_surface_commit(output->surface);

	// Roundtrip to receive first configure as required by
	// the xdg-shell protocol
	wl_display_roundtrip(wl->remote_display);

	wl_list_insert(&wl->outputs, &output->link);
	wlr_output_update_enabled(wlr_output, true);

	wlr_signal_emit_safe(&backend->events.new_output, wlr_output);

	if (wl->pointer != NULL) {
		create_wl_pointer(wl->pointer, output);
	}

	wlr_output_send_frame(wlr_output);
	return wlr_output;

error:
	wlr_output_destroy(&output->wlr_output);
	return NULL;
}

void wlr_wl_output_set_title(struct wlr_output *output, const char *title) {
	struct wlr_wl_output *wl_output = get_wl_output_from_output(output);

	char wl_title[32];
	if (title == NULL) {
		if (snprintf(wl_title, sizeof(wl_title), "wlroots - %s", output->name) <= 0) {
			return;
		}
		title = wl_title;
	}

	xdg_toplevel_set_title(wl_output->xdg_toplevel, title);
}
