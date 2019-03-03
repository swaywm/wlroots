#define _XOPEN_SOURCE 700

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <xcb/xcb.h>
#include <xcb/present.h>
#include <xcb/xinput.h>

#include <wlr/backend/interface.h>
#include <wlr/interfaces/wlr_output.h>
#include <wlr/interfaces/wlr_pointer.h>
#include <wlr/util/log.h>

#include "backend/x11.h"
#include "util/signal.h"

void handle_x11_present_event(struct wlr_x11_backend *x11,
		xcb_present_generic_event_t *e) {
	struct wlr_x11_output *output;
	xcb_present_configure_notify_event_t *configure;
	xcb_present_complete_notify_event_t *complete;
	xcb_present_idle_notify_event_t *idle;

	switch (e->evtype) {
	case XCB_PRESENT_CONFIGURE_NOTIFY:
		configure = (xcb_present_configure_notify_event_t *)e;
		output = get_x11_output_from_window_id(x11, configure->window);

		if (configure->width > 0 && configure->height > 0) {
			wlr_output_update_custom_mode(&output->wlr_output,
				configure->width, configure->height, 0);

			// Move the pointer to its new location
			update_x11_pointer_position(output, output->x11->time);
		}
		break;
	case XCB_PRESENT_COMPLETE_NOTIFY:
		complete = (xcb_present_complete_notify_event_t *)e;
		output = get_x11_output_from_window_id(x11, complete->window);

		if (!output) {
			return;
		}

		if (complete->msc > output->msc) {
			xcb_present_notify_msc(x11->xcb, output->win, 0, complete->msc + 1, 1, 0);
			wlr_output_send_frame(&output->wlr_output);
		}
		output->msc = complete->msc;
		break;
	case XCB_PRESENT_IDLE_NOTIFY:
		idle = (xcb_present_idle_notify_event_t *)e;
		output = get_x11_output_from_window_id(x11, idle->window);

		// This can happen after a window is destroyed
		if (!output) {
			return;
		}

		int i = ffs(idle->serial) - 1;
		assert(i >= 0 && i < 8);

		struct wlr_image *img = output->images[i];
		output->serials &= ~(1 << i);

		// img may be null if the image gets freed (e.g. on resize)
		if (img) {
			wl_signal_emit(&img->release, img);
			output->images[i] = NULL;
		}
		break;
	}
}

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

	if (refresh <= 0) {
		refresh = X11_DEFAULT_REFRESH;
	}

	wlr_output_update_custom_mode(&output->wlr_output, wlr_output->width,
		wlr_output->height, refresh);
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

static void output_transform(struct wlr_output *wlr_output,
		enum wl_output_transform transform) {
	struct wlr_x11_output *output = get_x11_output_from_output(wlr_output);
	output->wlr_output.transform = transform;
}

static void output_destroy(struct wlr_output *wlr_output) {
	struct wlr_x11_output *output = get_x11_output_from_output(wlr_output);
	struct wlr_x11_backend *x11 = output->x11;

	wlr_input_device_destroy(&output->pointer_dev);

	wl_list_remove(&output->link);
	xcb_destroy_window(x11->xcb, output->win);
	xcb_flush(x11->xcb);
	free(output);
}

static const struct wlr_format_set *output_get_formats(struct wlr_output *wlr_output) {
	struct wlr_x11_output *output = get_x11_output_from_output(wlr_output);

	return &output->x11->formats;
}

static bool output_present(struct wlr_output *wlr_output) {
	struct wlr_x11_output *output = get_x11_output_from_output(wlr_output);
	struct wlr_x11_backend *x11 = output->x11;

	if (!wlr_output->image) {
		xcb_unmap_window(x11->xcb, output->win);
		xcb_flush(x11->xcb);
		return true;
	}

	xcb_pixmap_t pixmap = (xcb_pixmap_t)(uintptr_t)wlr_output->image->backend_priv;

	unsigned i;
	for (i = 0; i < 8; ++i) {
		if (!(output->serials & (1 << i))) {
			break;
		}
	}

	if (i == 8) {
		wlr_log(WLR_ERROR, "Too many outstanding images");
		return false;
	}

	output->serials |= 1 << i;
	output->images[i] = wlr_output->image;

	xcb_map_window(x11->xcb, output->win);
	xcb_void_cookie_t cookie = xcb_present_pixmap_checked(x11->xcb, output->win,
		pixmap, 1 << i, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, NULL);
	xcb_flush(x11->xcb);

	xcb_generic_error_t *err = xcb_request_check(x11->xcb, cookie);
	if (err) {
		wlr_log(WLR_ERROR, "Failed to present pixmap");
		free(err);
		return false;
	}

	return true;
}

static const struct wlr_output_impl output_impl = {
	.set_custom_mode = output_set_custom_mode,
	.transform = output_transform,
	.destroy = output_destroy,
	.get_formats = output_get_formats,
	.present = output_present,
};

struct wlr_output *wlr_x11_output_create(struct wlr_backend *backend) {
	struct wlr_x11_backend *x11 = get_x11_backend_from_backend(backend);

	if (!x11->started) {
		++x11->requested_outputs;
		return NULL;
	}

	struct wlr_x11_output *output = calloc(1, sizeof(*output));
	if (!output) {
		return NULL;
	}
	output->x11 = x11;

	struct wlr_output *wlr_output = &output->wlr_output;
	wlr_output_init(wlr_output, &x11->backend, &output_impl, x11->wl_display);

	wlr_output->width = 1024;
	wlr_output->height = 768;

	output_set_refresh(&output->wlr_output, 0);

	snprintf(wlr_output->name, sizeof(wlr_output->name), "X11-%d",
		wl_list_length(&x11->outputs) + 1);
	parse_xcb_setup(wlr_output, x11->xcb);

	uint32_t mask = XCB_CW_EVENT_MASK;
	uint32_t values[] = {
		XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_STRUCTURE_NOTIFY
	};
	output->win = xcb_generate_id(x11->xcb);
	xcb_create_window(x11->xcb, XCB_COPY_FROM_PARENT, output->win,
		x11->screen->root, 0, 0, wlr_output->width, wlr_output->height, 1,
		XCB_WINDOW_CLASS_INPUT_OUTPUT, x11->screen->root_visual, mask, values);

	uint32_t present_mask = XCB_PRESENT_EVENT_MASK_CONFIGURE_NOTIFY |
		XCB_PRESENT_EVENT_MASK_COMPLETE_NOTIFY |
		XCB_PRESENT_EVENT_MASK_IDLE_NOTIFY;
	output->present_id = xcb_generate_id(x11->xcb);
	xcb_present_select_input(x11->xcb, output->present_id,
		output->win, present_mask);

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
			XCB_INPUT_XI_EVENT_MASK_LEAVE,
	};
	xcb_input_xi_select_events(x11->xcb, output->win, 1, &xinput_mask.head);

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

	wlr_signal_emit_safe(&x11->backend.events.new_output, wlr_output);
	wlr_signal_emit_safe(&x11->backend.events.new_input, &output->pointer_dev);

	wlr_output_send_frame(wlr_output);
	return wlr_output;
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
