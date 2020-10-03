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
#include "linux-dmabuf-unstable-v1-client-protocol.h"
#include "presentation-time-client-protocol.h"
#include "xdg-decoration-unstable-v1-client-protocol.h"
#include "xdg-shell-client-protocol.h"

static struct wlr_wl_output *get_wl_output_from_output(
		struct wlr_output *wlr_output) {
	assert(wlr_output_is_wl(wlr_output));
	return (struct wlr_wl_output *)wlr_output;
}

static void surface_frame_callback(void *data, struct wl_callback *cb,
		uint32_t time) {
	struct wlr_wl_output *output = data;
	assert(output);
	wl_callback_destroy(cb);
	output->frame_callback = NULL;

	wlr_output_send_frame(&output->wlr_output);
}

static const struct wl_callback_listener frame_listener = {
	.done = surface_frame_callback
};

static void presentation_feedback_destroy(
		struct wlr_wl_presentation_feedback *feedback) {
	wl_list_remove(&feedback->link);
	wp_presentation_feedback_destroy(feedback->feedback);
	free(feedback);
}

static void presentation_feedback_handle_sync_output(void *data,
		struct wp_presentation_feedback *feedback, struct wl_output *output) {
	// This space is intentionally left blank
}

static void presentation_feedback_handle_presented(void *data,
		struct wp_presentation_feedback *wp_feedback, uint32_t tv_sec_hi,
		uint32_t tv_sec_lo, uint32_t tv_nsec, uint32_t refresh_ns,
		uint32_t seq_hi, uint32_t seq_lo, uint32_t flags) {
	struct wlr_wl_presentation_feedback *feedback = data;

	struct timespec t = {
		.tv_sec = ((uint64_t)tv_sec_hi << 32) | tv_sec_lo,
		.tv_nsec = tv_nsec,
	};
	struct wlr_output_event_present event = {
		.commit_seq = feedback->commit_seq,
		.when = &t,
		.seq = ((uint64_t)seq_hi << 32) | seq_lo,
		.refresh = refresh_ns,
		.flags = flags,
	};
	wlr_output_send_present(&feedback->output->wlr_output, &event);

	presentation_feedback_destroy(feedback);
}

static void presentation_feedback_handle_discarded(void *data,
		struct wp_presentation_feedback *wp_feedback) {
	struct wlr_wl_presentation_feedback *feedback = data;

	wlr_output_send_present(&feedback->output->wlr_output, NULL);

	presentation_feedback_destroy(feedback);
}

static const struct wp_presentation_feedback_listener
		presentation_feedback_listener = {
	.sync_output = presentation_feedback_handle_sync_output,
	.presented = presentation_feedback_handle_presented,
	.discarded = presentation_feedback_handle_discarded,
};

static bool output_set_custom_mode(struct wlr_output *wlr_output,
		int32_t width, int32_t height, int32_t refresh) {
	struct wlr_wl_output *output = get_wl_output_from_output(wlr_output);
	wl_egl_window_resize(output->egl_window, width, height, 0, 0);
	wlr_output_update_custom_mode(&output->wlr_output, width, height, 0);
	return true;
}

static bool output_attach_render(struct wlr_output *wlr_output,
		int *buffer_age) {
	struct wlr_wl_output *output =
		get_wl_output_from_output(wlr_output);
	return wlr_egl_make_current(&output->backend->egl, output->egl_surface,
		buffer_age);
}

static void destroy_wl_buffer(struct wlr_wl_buffer *buffer) {
	if (buffer == NULL) {
		return;
	}
	wl_buffer_destroy(buffer->wl_buffer);
	wlr_buffer_unlock(buffer->buffer);
	free(buffer);
}

static void buffer_handle_release(void *data, struct wl_buffer *wl_buffer) {
	struct wlr_wl_buffer *buffer = data;
	destroy_wl_buffer(buffer);
}

static const struct wl_buffer_listener buffer_listener = {
	.release = buffer_handle_release,
};

static bool test_buffer(struct wlr_wl_backend *wl,
		struct wlr_buffer *wlr_buffer) {
	struct wlr_dmabuf_attributes attribs;
	if (!wlr_buffer_get_dmabuf(wlr_buffer, &attribs)) {
		return false;
	}

	if (!wlr_drm_format_set_has(&wl->linux_dmabuf_v1_formats,
			attribs.format, attribs.modifier)) {
		return false;
	}

	return true;
}

static struct wlr_wl_buffer *create_wl_buffer(struct wlr_wl_backend *wl,
		struct wlr_buffer *wlr_buffer) {
	if (!test_buffer(wl, wlr_buffer)) {
		return NULL;
	}

	struct wlr_dmabuf_attributes attribs;
	if (!wlr_buffer_get_dmabuf(wlr_buffer, &attribs)) {
		return NULL;
	}

	uint32_t modifier_hi = attribs.modifier >> 32;
	uint32_t modifier_lo = (uint32_t)attribs.modifier;
	struct zwp_linux_buffer_params_v1 *params =
		zwp_linux_dmabuf_v1_create_params(wl->zwp_linux_dmabuf_v1);
	for (int i = 0; i < attribs.n_planes; i++) {
		zwp_linux_buffer_params_v1_add(params, attribs.fd[i], i,
			attribs.offset[i], attribs.stride[i], modifier_hi, modifier_lo);
	}

	uint32_t flags = 0;
	if (attribs.flags & WLR_DMABUF_ATTRIBUTES_FLAGS_Y_INVERT) {
		flags |= ZWP_LINUX_BUFFER_PARAMS_V1_FLAGS_Y_INVERT;
	}
	if (attribs.flags & WLR_DMABUF_ATTRIBUTES_FLAGS_INTERLACED) {
		flags |= ZWP_LINUX_BUFFER_PARAMS_V1_FLAGS_INTERLACED;
	}
	if (attribs.flags & WLR_DMABUF_ATTRIBUTES_FLAGS_BOTTOM_FIRST) {
		flags |= ZWP_LINUX_BUFFER_PARAMS_V1_FLAGS_BOTTOM_FIRST;
	}
	struct wl_buffer *wl_buffer = zwp_linux_buffer_params_v1_create_immed(
		params, attribs.width, attribs.height, attribs.format, flags);
	// TODO: handle create() errors

	struct wlr_wl_buffer *buffer = calloc(1, sizeof(struct wlr_wl_buffer));
	if (buffer == NULL) {
		wl_buffer_destroy(wl_buffer);
		return NULL;
	}
	buffer->wl_buffer = wl_buffer;
	buffer->buffer = wlr_buffer_lock(wlr_buffer);

	wl_buffer_add_listener(wl_buffer, &buffer_listener, buffer);

	return buffer;
}

static bool output_test(struct wlr_output *wlr_output) {
	struct wlr_wl_output *output =
		get_wl_output_from_output(wlr_output);

	if (wlr_output->pending.committed & WLR_OUTPUT_STATE_ENABLED) {
		wlr_log(WLR_DEBUG, "Cannot disable a Wayland output");
		return false;
	}

	if (wlr_output->pending.committed & WLR_OUTPUT_STATE_MODE) {
		assert(wlr_output->pending.mode_type == WLR_OUTPUT_STATE_MODE_CUSTOM);
	}

	if ((wlr_output->pending.committed & WLR_OUTPUT_STATE_BUFFER) &&
			wlr_output->pending.buffer_type == WLR_OUTPUT_STATE_BUFFER_SCANOUT &&
			!test_buffer(output->backend, wlr_output->pending.buffer)) {
		return false;
	}

	return true;
}

static bool output_commit(struct wlr_output *wlr_output) {
	struct wlr_wl_output *output =
		get_wl_output_from_output(wlr_output);

	if (!output_test(wlr_output)) {
		return false;
	}

	if (wlr_output->pending.committed & WLR_OUTPUT_STATE_MODE) {
		if (!output_set_custom_mode(wlr_output,
				wlr_output->pending.custom_mode.width,
				wlr_output->pending.custom_mode.height,
				wlr_output->pending.custom_mode.refresh)) {
			return false;
		}
	}

	if (wlr_output->pending.committed & WLR_OUTPUT_STATE_BUFFER) {
		struct wp_presentation_feedback *wp_feedback = NULL;
		if (output->backend->presentation != NULL) {
			wp_feedback = wp_presentation_feedback(output->backend->presentation,
				output->surface);
		}

		pixman_region32_t *damage = NULL;
		if (wlr_output->pending.committed & WLR_OUTPUT_STATE_DAMAGE) {
			damage = &wlr_output->pending.damage;
		}

		if (output->frame_callback != NULL) {
			wlr_log(WLR_ERROR, "Skipping buffer swap");
			return false;
		}

		output->frame_callback = wl_surface_frame(output->surface);
		wl_callback_add_listener(output->frame_callback, &frame_listener, output);

		switch (wlr_output->pending.buffer_type) {
		case WLR_OUTPUT_STATE_BUFFER_RENDER:
			if (!wlr_egl_swap_buffers(&output->backend->egl,
					output->egl_surface, damage)) {
				return false;
			}
			break;
		case WLR_OUTPUT_STATE_BUFFER_SCANOUT:;
			struct wlr_wl_buffer *buffer =
				create_wl_buffer(output->backend, wlr_output->pending.buffer);
			if (buffer == NULL) {
				return false;
			}

			wl_surface_attach(output->surface, buffer->wl_buffer, 0, 0);

			if (damage == NULL) {
				wl_surface_damage_buffer(output->surface,
					0, 0, INT32_MAX, INT32_MAX);
			} else {
				int rects_len;
				pixman_box32_t *rects =
					pixman_region32_rectangles(damage, &rects_len);
				for (int i = 0; i < rects_len; i++) {
					pixman_box32_t *r = &rects[i];
					wl_surface_damage_buffer(output->surface, r->x1, r->y1,
						r->x2 - r->x1, r->y2 - r->y1);
				}
			}
			wl_surface_commit(output->surface);
			break;
		}

		if (wp_feedback != NULL) {
			struct wlr_wl_presentation_feedback *feedback =
				calloc(1, sizeof(*feedback));
			if (feedback == NULL) {
				wp_presentation_feedback_destroy(wp_feedback);
				return false;
			}
			feedback->output = output;
			feedback->feedback = wp_feedback;
			feedback->commit_seq = output->wlr_output.commit_seq + 1;
			wl_list_insert(&output->presentation_feedbacks, &feedback->link);

			wp_presentation_feedback_add_listener(wp_feedback,
				&presentation_feedback_listener, feedback);
		} else {
			wlr_output_send_present(wlr_output, NULL);
		}
	}

	return true;
}

static void output_rollback_render(struct wlr_output *wlr_output) {
	struct wlr_wl_output *output =
		get_wl_output_from_output(wlr_output);
	wlr_egl_unset_current(&output->backend->egl);
}

static bool output_set_cursor(struct wlr_output *wlr_output,
		struct wlr_texture *texture, float scale,
		enum wl_output_transform transform,
		int32_t hotspot_x, int32_t hotspot_y, bool update_texture) {
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
	return true;
}

static void output_destroy(struct wlr_output *wlr_output) {
	struct wlr_wl_output *output = get_wl_output_from_output(wlr_output);
	if (output == NULL) {
		return;
	}

	wl_list_remove(&output->link);

	if (output->cursor.egl_window != NULL) {
		wl_egl_window_destroy(output->cursor.egl_window);
	}
	if (output->cursor.surface) {
		wl_surface_destroy(output->cursor.surface);
	}

	if (output->frame_callback) {
		wl_callback_destroy(output->frame_callback);
	}

	struct wlr_wl_presentation_feedback *feedback, *feedback_tmp;
	wl_list_for_each_safe(feedback, feedback_tmp,
			&output->presentation_feedbacks, link) {
		presentation_feedback_destroy(feedback);
	}

	wlr_egl_destroy_surface(&output->backend->egl, output->egl_surface);
	wl_egl_window_destroy(output->egl_window);
	if (output->zxdg_toplevel_decoration_v1) {
		zxdg_toplevel_decoration_v1_destroy(output->zxdg_toplevel_decoration_v1);
	}
	xdg_toplevel_destroy(output->xdg_toplevel);
	xdg_surface_destroy(output->xdg_surface);
	wl_surface_destroy(output->surface);
	free(output);
}

void update_wl_output_cursor(struct wlr_wl_output *output) {
	struct wlr_wl_pointer *pointer = output->backend->current_pointer;
	if (pointer && pointer->output == output && output->enter_serial) {
		wl_pointer_set_cursor(pointer->wl_pointer, output->enter_serial,
			output->cursor.surface, output->cursor.hotspot_x,
			output->cursor.hotspot_y);
	}
}

static bool output_move_cursor(struct wlr_output *_output, int x, int y) {
	// TODO: only return true if x == current x and y == current y
	return true;
}

static const struct wlr_output_impl output_impl = {
	.destroy = output_destroy,
	.attach_render = output_attach_render,
	.test = output_test,
	.commit = output_commit,
	.rollback_render = output_rollback_render,
	.set_cursor = output_set_cursor,
	.move_cursor = output_move_cursor,
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
	snprintf(wlr_output->name, sizeof(wlr_output->name), "WL-%zd",
		++backend->last_output_num);

	char description[128];
	snprintf(description, sizeof(description),
		"Wayland output %zd", backend->last_output_num);
	wlr_output_set_description(wlr_output, description);

	output->backend = backend;
	wl_list_init(&output->presentation_feedbacks);

	output->surface = wl_compositor_create_surface(backend->compositor);
	if (!output->surface) {
		wlr_log_errno(WLR_ERROR, "Could not create output surface");
		goto error;
	}
	wl_surface_set_user_data(output->surface, output);
	output->xdg_surface =
		xdg_wm_base_get_xdg_surface(backend->xdg_wm_base, output->surface);
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

	if (backend->zxdg_decoration_manager_v1) {
		output->zxdg_toplevel_decoration_v1 =
			zxdg_decoration_manager_v1_get_toplevel_decoration(
			backend->zxdg_decoration_manager_v1, output->xdg_toplevel);
		if (!output->zxdg_toplevel_decoration_v1) {
			wlr_log_errno(WLR_ERROR, "Could not get xdg toplevel decoration");
			goto error;
		}
		zxdg_toplevel_decoration_v1_set_mode(output->zxdg_toplevel_decoration_v1,
			ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
	}

	wlr_wl_output_set_title(wlr_output, NULL);

	xdg_toplevel_set_app_id(output->xdg_toplevel, "wlroots");
	xdg_surface_add_listener(output->xdg_surface,
			&xdg_surface_listener, output);
	xdg_toplevel_add_listener(output->xdg_toplevel,
			&xdg_toplevel_listener, output);
	wl_surface_commit(output->surface);

	output->egl_window = wl_egl_window_create(output->surface,
			wlr_output->width, wlr_output->height);
	output->egl_surface = wlr_egl_create_surface(&backend->egl,
		output->egl_window);

	wl_display_roundtrip(output->backend->remote_display);

	// start rendering loop per callbacks by rendering first frame
	if (!wlr_egl_make_current(&output->backend->egl, output->egl_surface,
			NULL)) {
		goto error;
	}

	wlr_renderer_begin(backend->renderer, wlr_output->width, wlr_output->height);
	wlr_renderer_clear(backend->renderer, (float[]){ 1.0, 1.0, 1.0, 1.0 });
	wlr_renderer_end(backend->renderer);

	output->frame_callback = wl_surface_frame(output->surface);
	wl_callback_add_listener(output->frame_callback, &frame_listener, output);

	if (!wlr_egl_swap_buffers(&output->backend->egl, output->egl_surface,
			NULL)) {
		goto error;
	}

	wl_list_insert(&backend->outputs, &output->link);
	wlr_output_update_enabled(wlr_output, true);

	wlr_signal_emit_safe(&backend->backend.events.new_output, wlr_output);

	struct wlr_wl_seat *seat = backend->seat;
	if (seat != NULL) {
		if (seat->pointer) {
			create_wl_pointer(seat->pointer, output);
		}
	}

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

struct wl_surface *wlr_wl_output_get_surface(struct wlr_output *output) {
	struct wlr_wl_output *wl_output = get_wl_output_from_output(output);
	return wl_output->surface;
}
