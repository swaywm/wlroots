#include <stdbool.h>
#include <stdlib.h>
#include <xcb/xcb.h>
#include <X11/Xlib-xcb.h>
#include <wayland-server.h>
#include <wlr/backend/interface.h>
#include <wlr/backend/x11.h>
#include <wlr/egl.h>
#include <wlr/util/log.h>
#include "backend/x11.h"

static struct wlr_backend_impl backend_impl;

int x11_event(int fd, uint32_t mask, void *data) {
	struct wlr_x11_backend *x11 = data;

	xcb_generic_event_t *event = xcb_wait_for_event(x11->xcb_conn);
	if (!event) {
		return 0;
	}

	switch (event->response_type) {
	case XCB_EXPOSE:
		break;
	case XCB_KEY_PRESS:
		break;
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

	return &x11->backend;

error_event:
	wl_event_source_remove(x11->event_source);
error_x11:
	xcb_disconnect(x11->xcb_conn);
	XCloseDisplay(x11->xlib_conn);
	free(x11);
	return NULL;
}

static bool wlr_x11_backend_start(struct wlr_backend *backend) {
	struct wlr_x11_backend *x11 = (struct wlr_x11_backend *)backend;

	uint32_t mask = XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK;
	uint32_t values[2] = {
		x11->screen->white_pixel,
		XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_KEY_PRESS
	};

	x11->win = xcb_generate_id(x11->xcb_conn);

	xcb_create_window(x11->xcb_conn, XCB_COPY_FROM_PARENT, x11->win, x11->screen->root,
		0, 0, 1024, 768, 1, XCB_WINDOW_CLASS_INPUT_OUTPUT,
		x11->screen->root_visual, mask, values);

	xcb_map_window(x11->xcb_conn, x11->win);
	xcb_flush(x11->xcb_conn);

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
