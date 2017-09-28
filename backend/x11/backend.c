#define _POSIX_C_SOURCE 199309L
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <EGL/egl.h>
#include <wayland-server.h>
#include <xcb/xcb.h>
#include <X11/Xlib-xcb.h>
#include <wlr/backend/interface.h>
#include <wlr/backend/x11.h>
#include <wlr/egl.h>
#include <wlr/interfaces/wlr_output.h>
#include <wlr/interfaces/wlr_input_device.h>
#include <wlr/interfaces/wlr_keyboard.h>
#include <wlr/interfaces/wlr_pointer.h>
#include <wlr/util/log.h>
#include "backend/x11.h"

static struct wlr_backend_impl backend_impl;
static struct wlr_input_device_impl input_impl;
static struct wlr_keyboard_impl keyboard_impl;
static struct wlr_pointer_impl pointer_impl;

int x11_event(int fd, uint32_t mask, void *data) {
	struct wlr_x11_backend *x11 = data;

	xcb_generic_event_t *event = xcb_wait_for_event(x11->xcb_conn);
	if (!event) {
		return 0;
	}

	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);

	switch (event->response_type) {
		struct wlr_x11_output *output;

	case XCB_EXPOSE:
		output = &x11->output;
		wl_signal_emit(&output->wlr_output.events.frame, output);
		break;
	case XCB_KEY_PRESS: {
		xcb_key_press_event_t *press = (xcb_key_press_event_t *)event;
		struct wlr_event_keyboard_key key = {
			.time_sec = ts.tv_sec,
			.time_usec = ts.tv_nsec / 1000,
			.keycode = press->detail - 8,
			.state = WLR_KEY_PRESSED,
		};

		wl_signal_emit(&x11->keyboard.events.key, &key);
		break;
	}
	case XCB_KEY_RELEASE: {
		xcb_key_release_event_t *press = (xcb_key_release_event_t *)event;
		struct wlr_event_keyboard_key key = {
			.time_sec = ts.tv_sec,
			.time_usec = ts.tv_nsec / 1000,
			.keycode = press->detail - 8,
			.state = WLR_KEY_RELEASED,
		};

		wl_signal_emit(&x11->keyboard.events.key, &key);
		break;
	}
	default:
		wlr_log(L_INFO, "Unknown event");
		break;
	}

	free(event);
	return 0;
}

struct wlr_backend *wlr_x11_backend_create(struct wl_display *display,
		const char *x11_display) {
	struct wlr_x11_backend *x11 = calloc(1, sizeof(*x11));
	if (!x11) {
		return NULL;
	}

	wlr_backend_init(&x11->backend, &backend_impl);

	x11->xlib_conn = XOpenDisplay(x11_display);
	if (!x11->xlib_conn) {
		wlr_log(L_ERROR, "Failed to open X connection");
		return NULL;
	}

	x11->xcb_conn = XGetXCBConnection(x11->xlib_conn);
	if (!x11->xcb_conn || xcb_connection_has_error(x11->xcb_conn)) {
		wlr_log(L_ERROR, "Failed to open xcb connection");
		goto error_x11;
	}

	XSetEventQueueOwner(x11->xlib_conn, XCBOwnsEventQueue);

	int fd = xcb_get_file_descriptor(x11->xcb_conn);
	struct wl_event_loop *ev = wl_display_get_event_loop(display);
	x11->event_source = wl_event_loop_add_fd(ev, fd, WL_EVENT_READABLE, x11_event, x11);
	if (!x11->event_source) {
		wlr_log(L_ERROR, "Could not create event source");
		goto error_x11;
	}

	x11->screen = xcb_setup_roots_iterator(xcb_get_setup(x11->xcb_conn)).data;

	if (!wlr_egl_init(&x11->egl, EGL_PLATFORM_X11_KHR,
			x11->screen->root_visual, x11->xlib_conn)) {
		goto error_event;
	}

	wlr_input_device_init(&x11->keyboard_dev, WLR_INPUT_DEVICE_KEYBOARD,
		&input_impl, "X11 keyboard", 0, 0);
	wlr_keyboard_init(&x11->keyboard, &keyboard_impl);
	x11->keyboard_dev.keyboard = &x11->keyboard;

	wlr_input_device_init(&x11->pointer_dev, WLR_INPUT_DEVICE_POINTER,
		&input_impl, "X11 pointer", 0, 0);
	wlr_pointer_init(&x11->pointer, &pointer_impl);
	x11->pointer_dev.pointer = &x11->pointer;

	return &x11->backend;

error_event:
	wl_event_source_remove(x11->event_source);
error_x11:
	xcb_disconnect(x11->xcb_conn);
	XCloseDisplay(x11->xlib_conn);
	free(x11);
	return NULL;
}

static struct wlr_output_impl output_impl;

static bool wlr_x11_backend_start(struct wlr_backend *backend) {
	struct wlr_x11_backend *x11 = (struct wlr_x11_backend *)backend;
	struct wlr_x11_output *output = &x11->output;

	uint32_t mask = XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK;
	uint32_t values[2] = {
		x11->screen->white_pixel,
		XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_KEY_RELEASE
	};

	output->x11 = x11;

	wlr_output_init(&output->wlr_output, &output_impl);
	snprintf(output->wlr_output.name, sizeof(output->wlr_output.name), "X11-1");

	output->win = xcb_generate_id(x11->xcb_conn);
	xcb_create_window(x11->xcb_conn, XCB_COPY_FROM_PARENT, output->win, x11->screen->root,
		0, 0, 1024, 768, 1, XCB_WINDOW_CLASS_INPUT_OUTPUT,
		x11->screen->root_visual, mask, values);

	output->surf = wlr_egl_create_surface(&x11->egl, &output->win);
	if (!output->surf) {
		wlr_log(L_ERROR, "Failed to create EGL surface");
		return false;
	}

	xcb_map_window(x11->xcb_conn, output->win);
	xcb_flush(x11->xcb_conn);

	wl_signal_emit(&x11->backend.events.output_add, output);
	wl_signal_emit(&x11->backend.events.input_add, &x11->keyboard_dev);
	wl_signal_emit(&x11->backend.events.input_add, &x11->pointer_dev);

	return true;
}

static void wlr_x11_backend_destroy(struct wlr_backend *backend) {
	if (!backend) {
		return;
	}

	struct wlr_x11_backend *x11 = (struct wlr_x11_backend *)backend;

	wlr_egl_free(&x11->egl);

	xcb_disconnect(x11->xcb_conn);
	free(x11);
}

static struct wlr_egl *wlr_x11_backend_get_egl(struct wlr_backend *backend) {
	struct wlr_x11_backend *x11 = (struct wlr_x11_backend *)backend;
	return &x11->egl;
}

bool wlr_backend_is_x11(struct wlr_backend *backend) {
	return backend->impl == &backend_impl;
}

static struct wlr_backend_impl backend_impl = {
	.start = wlr_x11_backend_start,
	.destroy = wlr_x11_backend_destroy,
	.get_egl = wlr_x11_backend_get_egl,
};

static void output_transform(struct wlr_output *output, enum wl_output_transform transform) {
	// TODO
}

static void output_destroy(struct wlr_output *wlr_output) {
	struct wlr_x11_output *output = (struct wlr_x11_output *)wlr_output;
	struct wlr_x11_backend *x11 = output->x11;

	eglDestroySurface(x11->egl.display, output->surf);
	xcb_destroy_window(x11->xcb_conn, output->win);
}

static void output_make_current(struct wlr_output *wlr_output) {
	struct wlr_x11_output *output = (struct wlr_x11_output *)wlr_output;
	struct wlr_x11_backend *x11 = output->x11;

	if (!eglMakeCurrent(x11->egl.display, output->surf, output->surf, x11->egl.context)) {
		wlr_log(L_ERROR, "eglMakeCurrent failed: %s", egl_error());
	}
}

static void output_swap_buffers(struct wlr_output *wlr_output) {
	struct wlr_x11_output *output = (struct wlr_x11_output *)wlr_output;
	struct wlr_x11_backend *x11 = output->x11;

	if (!eglSwapBuffers(x11->egl.display, output->surf)) {
		wlr_log(L_ERROR, "eglSwapBuffers failed: %s", egl_error());
	}
}

static struct wlr_output_impl output_impl = {
	.transform = output_transform,
	.destroy = output_destroy,
	.make_current = output_make_current,
	.swap_buffers = output_swap_buffers,
};

static void input_destroy(struct wlr_input_device *input_dev) {
	// Do nothing
}

static struct wlr_input_device_impl input_impl = {
	.destroy = input_destroy,
};

static void keyboard_destroy(struct wlr_keyboard *keyboard) {
	// Do nothing
}

static void keyboard_led_update(struct wlr_keyboard *keyboard, uint32_t leds) {
	// Do nothing
}

static struct wlr_keyboard_impl keyboard_impl = {
	.destroy = keyboard_destroy,
	.led_update = keyboard_led_update,
};

static void pointer_destroy(struct wlr_pointer *pointer) {
	// Do nothing
}

static struct wlr_pointer_impl pointer_impl = {
	.destroy = pointer_destroy,
};
