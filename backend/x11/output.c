#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include <drm_fourcc.h>
#include <xcb/dri3.h>
#include <xcb/present.h>
#include <xcb/xcb.h>
#include <xcb/xinput.h>

#include <wlr/interfaces/wlr_output.h>
#include <wlr/interfaces/wlr_pointer.h>
#include <wlr/interfaces/wlr_touch.h>
#include <wlr/render/egl.h>
#include <wlr/render/gles2.h>
#include <wlr/util/log.h>

#include "backend/x11.h"
#include "render/swapchain.h"
#include "render/wlr_renderer.h"
#include "util/signal.h"
#include "util/time.h"

static void parse_xcb_setup(struct wlr_output *output,
		xcb_connection_t *xcb) {
	const xcb_setup_t *xcb_setup = xcb_get_setup(xcb);

	snprintf(output->make, sizeof(output->make), "%.*s",
			xcb_setup_vendor_length(xcb_setup),
			xcb_setup_vendor(xcb_setup));
	snprintf(output->model, sizeof(output->model), "%"PRIu16".%"PRIu16,
			xcb_setup->protocol_major_version,
			xcb_setup->protocol_minor_version);
}

static struct wlr_x11_output *get_x11_output_from_output(
		struct wlr_output *wlr_output) {
	assert(wlr_output_is_x11(wlr_output));
	return (struct wlr_x11_output *)wlr_output;
}

static void output_set_refresh(struct wlr_output *wlr_output, int32_t refresh) {
	struct wlr_x11_output *output = get_x11_output_from_output(wlr_output);

	wlr_output_update_custom_mode(&output->wlr_output, wlr_output->width,
		wlr_output->height, 0);
}

static bool output_set_custom_mode(struct wlr_output *wlr_output,
		int32_t width, int32_t height, int32_t refresh) {
	struct wlr_x11_output *output = get_x11_output_from_output(wlr_output);
	struct wlr_x11_backend *x11 = output->x11;

	output_set_refresh(&output->wlr_output, refresh);

	const uint32_t values[] = { width, height };
	xcb_void_cookie_t cookie = xcb_configure_window_checked(
		x11->xcb, output->win,
		XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, values);

	xcb_generic_error_t *error;
	if ((error = xcb_request_check(x11->xcb, cookie))) {
		wlr_log(WLR_ERROR, "Could not set window size to %dx%d\n",
			width, height);
		free(error);
		return false;
	}

	return true;
}

static void destroy_x11_buffer(struct wlr_x11_buffer *buffer);

static void output_destroy(struct wlr_output *wlr_output) {
	struct wlr_x11_output *output = get_x11_output_from_output(wlr_output);
	struct wlr_x11_backend *x11 = output->x11;

	wlr_input_device_destroy(&output->pointer_dev);
	wlr_input_device_destroy(&output->touch_dev);

	struct wlr_x11_buffer *buffer, *buffer_tmp;
	wl_list_for_each_safe(buffer, buffer_tmp, &output->buffers, link) {
		destroy_x11_buffer(buffer);
	}

	wl_list_remove(&output->link);
	wlr_buffer_unlock(output->back_buffer);
	wlr_swapchain_destroy(output->swapchain);
	// A zero event mask deletes the event context
	xcb_present_select_input(x11->xcb, output->present_event_id, output->win, 0);
	xcb_destroy_window(x11->xcb, output->win);
	xcb_flush(x11->xcb);
	free(output);
}

static bool output_attach_render(struct wlr_output *wlr_output,
		int *buffer_age) {
	struct wlr_x11_output *output = get_x11_output_from_output(wlr_output);
	struct wlr_x11_backend *x11 = output->x11;
	struct wlr_egl *egl = wlr_gles2_renderer_get_egl(x11->renderer);

	wlr_buffer_unlock(output->back_buffer);
	output->back_buffer = wlr_swapchain_acquire(output->swapchain, buffer_age);
	if (!output->back_buffer) {
		return false;
	}

	if (!wlr_egl_make_current(egl)) {
		return false;
	}
	if (!wlr_renderer_bind_buffer(x11->renderer, output->back_buffer)) {
		return false;
	}

	return true;
}

static bool output_test(struct wlr_output *wlr_output) {
	if (wlr_output->pending.committed & WLR_OUTPUT_STATE_ENABLED) {
		wlr_log(WLR_DEBUG, "Cannot disable an X11 output");
		return false;
	}

	if (wlr_output->pending.committed & WLR_OUTPUT_STATE_MODE) {
		assert(wlr_output->pending.mode_type == WLR_OUTPUT_STATE_MODE_CUSTOM);
	}

	return true;
}

static void destroy_x11_buffer(struct wlr_x11_buffer *buffer) {
	if (!buffer) {
		return;
	}
	wl_list_remove(&buffer->buffer_destroy.link);
	wl_list_remove(&buffer->link);
	xcb_free_pixmap(buffer->x11->xcb, buffer->pixmap);
	free(buffer);
}

static void buffer_handle_buffer_destroy(struct wl_listener *listener,
		void *data) {
	struct wlr_x11_buffer *buffer =
		wl_container_of(listener, buffer, buffer_destroy);
	destroy_x11_buffer(buffer);
}

static struct wlr_x11_buffer *create_x11_buffer(struct wlr_x11_output *output,
		struct wlr_buffer *wlr_buffer) {
	struct wlr_x11_backend *x11 = output->x11;

	struct wlr_dmabuf_attributes attrs = {0};
	if (!wlr_buffer_get_dmabuf(wlr_buffer, &attrs)) {
		return NULL;
	}

	if (attrs.format != x11->x11_format->drm) {
		// The pixmap's depth must match the window's depth, otherwise Present
		// will throw a Match error
		return NULL;
	}

	// xcb closes the FDs after sending them, so we need to dup them here
	struct wlr_dmabuf_attributes dup_attrs = {0};
	if (!wlr_dmabuf_attributes_copy(&dup_attrs, &attrs)) {
		return NULL;
	}

	const struct wlr_x11_format *x11_fmt = x11->x11_format;
	xcb_pixmap_t pixmap = xcb_generate_id(x11->xcb);
	xcb_dri3_pixmap_from_buffers(x11->xcb, pixmap, output->win,
		attrs.n_planes, attrs.width, attrs.height, attrs.stride[0],
		attrs.offset[0], attrs.stride[1], attrs.offset[1], attrs.stride[2],
		attrs.offset[2], attrs.stride[3], attrs.offset[3], x11_fmt->depth,
		x11_fmt->bpp, attrs.modifier, dup_attrs.fd);

	struct wlr_x11_buffer *buffer = calloc(1, sizeof(struct wlr_x11_buffer));
	if (!buffer) {
		xcb_free_pixmap(x11->xcb, pixmap);
		return NULL;
	}
	buffer->buffer = wlr_buffer_lock(wlr_buffer);
	buffer->pixmap = pixmap;
	buffer->x11 = x11;
	wl_list_insert(&output->buffers, &buffer->link);

	buffer->buffer_destroy.notify = buffer_handle_buffer_destroy;
	wl_signal_add(&wlr_buffer->events.destroy, &buffer->buffer_destroy);

	return buffer;
}

static struct wlr_x11_buffer *get_or_create_x11_buffer(
		struct wlr_x11_output *output, struct wlr_buffer *wlr_buffer) {
	struct wlr_x11_buffer *buffer;
	wl_list_for_each(buffer, &output->buffers, link) {
		if (buffer->buffer == wlr_buffer) {
			wlr_buffer_lock(buffer->buffer);
			return buffer;
		}
	}

	return create_x11_buffer(output, wlr_buffer);
}

static bool output_commit_buffer(struct wlr_x11_output *output) {
	struct wlr_x11_backend *x11 = output->x11;
	struct wlr_egl *egl = wlr_gles2_renderer_get_egl(x11->renderer);

	assert(output->back_buffer != NULL);

	wlr_renderer_bind_buffer(x11->renderer, NULL);
	wlr_egl_unset_current(egl);

	struct wlr_x11_buffer *x11_buffer =
		get_or_create_x11_buffer(output, output->back_buffer);
	if (!x11_buffer) {
		goto error;
	}

	xcb_xfixes_region_t region = XCB_NONE;
	if (output->wlr_output.pending.committed & WLR_OUTPUT_STATE_DAMAGE) {
		pixman_region32_t *damage = &output->wlr_output.pending.damage;

		int rects_len = 0;
		pixman_box32_t *rects = pixman_region32_rectangles(damage, &rects_len);

		xcb_rectangle_t *xcb_rects = calloc(rects_len, sizeof(xcb_rectangle_t));
		if (!xcb_rects) {
			goto error;
		}

		for (int i = 0; i < rects_len; i++) {
			pixman_box32_t *box = &rects[i];
			xcb_rects[i] = (struct xcb_rectangle_t){
				.x = box->x1,
				.y = box->y1,
				.width = box->x2 - box->x1,
				.height = box->y2 - box->y1,
			};
		}

		xcb_xfixes_region_t region = xcb_generate_id(x11->xcb);
		xcb_xfixes_create_region(x11->xcb, region, rects_len, xcb_rects);

		free(xcb_rects);
	}

	uint32_t serial = output->wlr_output.commit_seq;
	uint32_t options = 0;
	uint64_t target_msc = output->last_msc ? output->last_msc + 1 : 0;
	xcb_present_pixmap(x11->xcb, output->win, x11_buffer->pixmap, serial,
		0, region, 0, 0, XCB_NONE, XCB_NONE, XCB_NONE, options, target_msc,
		0, 0, 0, NULL);

	if (region != XCB_NONE) {
		xcb_xfixes_destroy_region(x11->xcb, region);
	}

	wlr_buffer_unlock(output->back_buffer);
	output->back_buffer = NULL;

	wlr_swapchain_set_buffer_submitted(output->swapchain, x11_buffer->buffer);

	return true;

error:
	destroy_x11_buffer(x11_buffer);
	wlr_buffer_unlock(output->back_buffer);
	output->back_buffer = NULL;
	return false;
}

static bool output_commit(struct wlr_output *wlr_output) {
	struct wlr_x11_output *output = get_x11_output_from_output(wlr_output);
	struct wlr_x11_backend *x11 = output->x11;

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

	if (wlr_output->pending.committed & WLR_OUTPUT_STATE_ADAPTIVE_SYNC_ENABLED &&
			x11->atoms.variable_refresh != XCB_ATOM_NONE) {
		if (wlr_output->pending.adaptive_sync_enabled) {
			uint32_t enabled = 1;
			xcb_change_property(x11->xcb, XCB_PROP_MODE_REPLACE, output->win,
				x11->atoms.variable_refresh, XCB_ATOM_CARDINAL, 32, 1,
				&enabled);
			wlr_output->adaptive_sync_status = WLR_OUTPUT_ADAPTIVE_SYNC_UNKNOWN;
		} else {
			xcb_delete_property(x11->xcb, output->win,
				x11->atoms.variable_refresh);
			wlr_output->adaptive_sync_status = WLR_OUTPUT_ADAPTIVE_SYNC_DISABLED;
		}
	}

	if (wlr_output->pending.committed & WLR_OUTPUT_STATE_BUFFER) {
		if (!output_commit_buffer(output)) {
			return false;
		}
	}

	xcb_flush(x11->xcb);

	return true;
}

static void output_rollback_render(struct wlr_output *wlr_output) {
	struct wlr_x11_output *output = get_x11_output_from_output(wlr_output);
	struct wlr_x11_backend *x11 = output->x11;
	struct wlr_egl *egl = wlr_gles2_renderer_get_egl(x11->renderer);

	wlr_renderer_bind_buffer(x11->renderer, NULL);
	wlr_egl_unset_current(egl);
}

static const struct wlr_output_impl output_impl = {
	.destroy = output_destroy,
	.attach_render = output_attach_render,
	.test = output_test,
	.commit = output_commit,
	.rollback_render = output_rollback_render,
};

struct wlr_output *wlr_x11_output_create(struct wlr_backend *backend) {
	struct wlr_x11_backend *x11 = get_x11_backend_from_backend(backend);

	if (!x11->started) {
		++x11->requested_outputs;
		return NULL;
	}

	struct wlr_x11_output *output = calloc(1, sizeof(struct wlr_x11_output));
	if (output == NULL) {
		return NULL;
	}
	output->x11 = x11;
	wl_list_init(&output->buffers);

	struct wlr_output *wlr_output = &output->wlr_output;
	wlr_output_init(wlr_output, &x11->backend, &output_impl, x11->wl_display);

	wlr_output->width = 1024;
	wlr_output->height = 768;

	output->swapchain = wlr_swapchain_create(x11->allocator,
		wlr_output->width, wlr_output->height, x11->drm_format);
	if (!output->swapchain) {
		wlr_log(WLR_ERROR, "Failed to create swapchain");
		free(output);
		return NULL;
	}

	output_set_refresh(&output->wlr_output, 0);

	snprintf(wlr_output->name, sizeof(wlr_output->name), "X11-%zd",
		++x11->last_output_num);
	parse_xcb_setup(wlr_output, x11->xcb);

	char description[128];
	snprintf(description, sizeof(description),
		"X11 output %zd", x11->last_output_num);
	wlr_output_set_description(wlr_output, description);

	// The X11 protocol requires us to set a colormap and border pixel if the
	// depth doesn't match the root window's
	uint32_t mask = XCB_CW_BORDER_PIXEL | XCB_CW_EVENT_MASK | XCB_CW_COLORMAP;
	uint32_t values[] = {
		0,
		XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_STRUCTURE_NOTIFY,
		x11->colormap,
	};
	output->win = xcb_generate_id(x11->xcb);
	xcb_create_window(x11->xcb, x11->depth->depth, output->win,
		x11->screen->root, 0, 0, wlr_output->width, wlr_output->height, 0,
		XCB_WINDOW_CLASS_INPUT_OUTPUT, x11->visualid, mask, values);

	struct {
		xcb_input_event_mask_t head;
		xcb_input_xi_event_mask_t mask;
	} xinput_mask = {
		.head = { .deviceid = XCB_INPUT_DEVICE_ALL_MASTER, .mask_len = 1 },
		.mask = XCB_INPUT_XI_EVENT_MASK_KEY_PRESS |
			XCB_INPUT_XI_EVENT_MASK_KEY_RELEASE |
			XCB_INPUT_XI_EVENT_MASK_BUTTON_PRESS |
			XCB_INPUT_XI_EVENT_MASK_BUTTON_RELEASE |
			XCB_INPUT_XI_EVENT_MASK_MOTION |
			XCB_INPUT_XI_EVENT_MASK_ENTER |
			XCB_INPUT_XI_EVENT_MASK_LEAVE |
			XCB_INPUT_XI_EVENT_MASK_TOUCH_BEGIN |
			XCB_INPUT_XI_EVENT_MASK_TOUCH_END |
			XCB_INPUT_XI_EVENT_MASK_TOUCH_UPDATE,
	};
	xcb_input_xi_select_events(x11->xcb, output->win, 1, &xinput_mask.head);

	uint32_t present_mask = XCB_PRESENT_EVENT_MASK_IDLE_NOTIFY |
		XCB_PRESENT_EVENT_MASK_COMPLETE_NOTIFY;
	output->present_event_id = xcb_generate_id(x11->xcb);
	xcb_present_select_input(x11->xcb, output->present_event_id, output->win,
		present_mask);

	xcb_change_property(x11->xcb, XCB_PROP_MODE_REPLACE, output->win,
		x11->atoms.wm_protocols, XCB_ATOM_ATOM, 32, 1,
		&x11->atoms.wm_delete_window);

	wlr_x11_output_set_title(wlr_output, NULL);

	xcb_map_window(x11->xcb, output->win);
	xcb_flush(x11->xcb);

	wl_list_insert(&x11->outputs, &output->link);

	wlr_output_update_enabled(wlr_output, true);

	wlr_input_device_init(&output->pointer_dev, WLR_INPUT_DEVICE_POINTER,
		&input_device_impl, "X11 pointer", 0, 0);
	wlr_pointer_init(&output->pointer, &pointer_impl);
	output->pointer_dev.pointer = &output->pointer;
	output->pointer_dev.output_name = strdup(wlr_output->name);

	wlr_input_device_init(&output->touch_dev, WLR_INPUT_DEVICE_TOUCH,
		&input_device_impl, "X11 touch", 0, 0);
	wlr_touch_init(&output->touch, &touch_impl);
	output->touch_dev.touch = &output->touch;
	output->touch_dev.output_name = strdup(wlr_output->name);
	wl_list_init(&output->touchpoints);

	wlr_signal_emit_safe(&x11->backend.events.new_output, wlr_output);
	wlr_signal_emit_safe(&x11->backend.events.new_input, &output->pointer_dev);
	wlr_signal_emit_safe(&x11->backend.events.new_input, &output->touch_dev);

	// Start the rendering loop by requesting the compositor to render a frame
	wlr_output_schedule_frame(wlr_output);

	return wlr_output;
}

void handle_x11_configure_notify(struct wlr_x11_output *output,
		xcb_configure_notify_event_t *ev) {
	// ignore events that set an invalid size:
	if (ev->width == 0 || ev->height == 0) {
		wlr_log(WLR_DEBUG,
			"Ignoring X11 configure event for height=%d, width=%d",
			ev->width, ev->height);
		return;
	}

	if (output->swapchain->width != ev->width ||
			output->swapchain->height != ev->height) {
		struct wlr_swapchain *swapchain = wlr_swapchain_create(
			output->x11->allocator, ev->width, ev->height,
			output->x11->drm_format);
		if (!swapchain) {
			return;
		}
		wlr_swapchain_destroy(output->swapchain);
		output->swapchain = swapchain;
	}

	wlr_output_update_custom_mode(&output->wlr_output, ev->width,
		ev->height, 0);

	// Move the pointer to its new location
	update_x11_pointer_position(output, output->x11->time);
}

bool wlr_output_is_x11(struct wlr_output *wlr_output) {
	return wlr_output->impl == &output_impl;
}

void wlr_x11_output_set_title(struct wlr_output *output, const char *title) {
	struct wlr_x11_output *x11_output = get_x11_output_from_output(output);

	char wl_title[32];
	if (title == NULL) {
		if (snprintf(wl_title, sizeof(wl_title), "wlroots - %s", output->name) <= 0) {
			return;
		}
		title = wl_title;
	}

	xcb_change_property(x11_output->x11->xcb, XCB_PROP_MODE_REPLACE, x11_output->win,
		x11_output->x11->atoms.net_wm_name, x11_output->x11->atoms.utf8_string, 8,
		strlen(title), title);
}

static struct wlr_x11_buffer *get_x11_buffer(struct wlr_x11_output *output,
		xcb_pixmap_t pixmap) {
	struct wlr_x11_buffer *buffer;
	wl_list_for_each(buffer, &output->buffers, link) {
		if (buffer->pixmap == pixmap) {
			return buffer;
		}
	}
	return NULL;
}

void handle_x11_present_event(struct wlr_x11_backend *x11,
		xcb_ge_generic_event_t *event) {
	struct wlr_x11_output *output;

	switch (event->event_type) {
	case XCB_PRESENT_EVENT_IDLE_NOTIFY:;
		xcb_present_idle_notify_event_t *idle_notify =
			(xcb_present_idle_notify_event_t *)event;

		output = get_x11_output_from_window_id(x11, idle_notify->window);
		if (!output) {
			wlr_log(WLR_DEBUG, "Got PresentIdleNotify event for unknown window");
			return;
		}

		struct wlr_x11_buffer *buffer =
			get_x11_buffer(output, idle_notify->pixmap);
		if (!buffer) {
			wlr_log(WLR_DEBUG, "Got PresentIdleNotify event for unknown buffer");
			return;
		}

		wlr_buffer_unlock(buffer->buffer); // may destroy buffer
		break;
	case XCB_PRESENT_COMPLETE_NOTIFY:;
		xcb_present_complete_notify_event_t *complete_notify =
			(xcb_present_complete_notify_event_t *)event;

		output = get_x11_output_from_window_id(x11, complete_notify->window);
		if (!output) {
			wlr_log(WLR_DEBUG, "Got PresentCompleteNotify event for unknown window");
			return;
		}

		output->last_msc = complete_notify->msc;

		struct timespec t;
		timespec_from_nsec(&t, complete_notify->ust * 1000);

		uint32_t flags = 0;
		if (complete_notify->mode == XCB_PRESENT_COMPLETE_MODE_FLIP) {
			flags |= WLR_OUTPUT_PRESENT_ZERO_COPY;
		}

		struct wlr_output_event_present present_event = {
			.output = &output->wlr_output,
			.commit_seq = complete_notify->serial,
			.when = &t,
			.seq = complete_notify->msc,
			.flags = flags,
		};
		wlr_output_send_present(&output->wlr_output, &present_event);

		wlr_output_send_frame(&output->wlr_output);
		break;
	default:
		wlr_log(WLR_DEBUG, "Unhandled Present event %"PRIu16, event->event_type);
	}
}
