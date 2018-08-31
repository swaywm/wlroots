#define _POSIX_C_SOURCE 200112L
#include <assert.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <wayland-server.h>
#include <wlr/backend/interface.h>
#include <wlr/backend/x11.h>
#include <wlr/config.h>
#include <wlr/interfaces/wlr_input_device.h>
#include <wlr/interfaces/wlr_keyboard.h>
#include <wlr/interfaces/wlr_pointer.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/util/log.h>
#include <X11/Xlib-xcb.h>
#include <xcb/xcb.h>
#ifdef WLR_HAS_XCB_XKB
#include <xcb/xkb.h>
#endif
#include "backend/x11.h"
#include "util/signal.h"

struct wlr_x11_output *get_x11_output_from_window_id(
		struct wlr_x11_backend *x11, xcb_window_t window) {
	struct wlr_x11_output *output;
	wl_list_for_each(output, &x11->outputs, link) {
		if (output->win == window) {
			return output;
		}
	}
	return NULL;
}

static void handle_x11_event(struct wlr_x11_backend *x11,
		xcb_generic_event_t *event) {
	handle_x11_input_event(x11, event);

	switch (event->response_type & XCB_EVENT_RESPONSE_TYPE_MASK) {
	case XCB_EXPOSE: {
		xcb_expose_event_t *ev = (xcb_expose_event_t *)event;
		struct wlr_x11_output *output =
			get_x11_output_from_window_id(x11, ev->window);
		if (output != NULL) {
			wlr_output_update_needs_swap(&output->wlr_output);
		}
		break;
	}
	case XCB_CONFIGURE_NOTIFY: {
		xcb_configure_notify_event_t *ev =
			(xcb_configure_notify_event_t *)event;
		struct wlr_x11_output *output =
			get_x11_output_from_window_id(x11, ev->window);
		if (output != NULL) {
			handle_x11_configure_notify(output, ev);
		}
		break;
	}
	case XCB_CLIENT_MESSAGE: {
		xcb_client_message_event_t *ev = (xcb_client_message_event_t *)event;
		if (ev->data.data32[0] == x11->atoms.wm_delete_window) {
			struct wlr_x11_output *output =
				get_x11_output_from_window_id(x11, ev->window);
			if (output != NULL) {
				wlr_output_destroy(&output->wlr_output);
			}
		}
		break;
	}
	}
}

static int x11_event(int fd, uint32_t mask, void *data) {
	struct wlr_x11_backend *x11 = data;

	if ((mask & WL_EVENT_HANGUP) || (mask & WL_EVENT_ERROR)) {
		wl_display_terminate(x11->wl_display);
		return 0;
	}

	xcb_generic_event_t *e;
	while ((e = xcb_poll_for_event(x11->xcb_conn))) {
		handle_x11_event(x11, e);
		free(e);
	}

	return 0;
}

struct wlr_x11_backend *get_x11_backend_from_backend(
		struct wlr_backend *wlr_backend) {
	assert(wlr_backend_is_x11(wlr_backend));
	return (struct wlr_x11_backend *)wlr_backend;
}

static bool backend_start(struct wlr_backend *backend) {
	struct wlr_x11_backend *x11 = get_x11_backend_from_backend(backend);
	x11->started = true;

	struct {
		const char *name;
		xcb_intern_atom_cookie_t cookie;
		xcb_atom_t *atom;
	} atom[] = {
		{
			.name = "WM_PROTOCOLS",
			.atom = &x11->atoms.wm_protocols,
		},
		{
			.name = "WM_DELETE_WINDOW",
			.atom = &x11->atoms.wm_delete_window,
		},
		{
			.name = "_NET_WM_NAME",
			.atom = &x11->atoms.net_wm_name,
		},
		{
			.name = "UTF8_STRING",
			.atom = &x11->atoms.utf8_string,
		},
	};

	for (size_t i = 0; i < sizeof(atom) / sizeof(atom[0]); ++i) {
		atom[i].cookie = xcb_intern_atom(x11->xcb_conn,
			true, strlen(atom[i].name), atom[i].name);
	}

	for (size_t i = 0; i < sizeof(atom) / sizeof(atom[0]); ++i) {
		xcb_intern_atom_reply_t *reply = xcb_intern_atom_reply(
			x11->xcb_conn, atom[i].cookie, NULL);

		if (reply) {
			*atom[i].atom = reply->atom;
			free(reply);
		} else {
			*atom[i].atom = XCB_ATOM_NONE;
		}
	}

	// create a blank cursor
	xcb_pixmap_t pix = xcb_generate_id(x11->xcb_conn);
	xcb_create_pixmap(x11->xcb_conn, 1, pix, x11->screen->root, 1, 1);

	x11->cursor = xcb_generate_id(x11->xcb_conn);
	xcb_create_cursor(x11->xcb_conn, x11->cursor, pix, pix, 0, 0, 0, 0, 0, 0,
		0, 0);
	xcb_free_pixmap(x11->xcb_conn, pix);

#ifdef WLR_HAS_XCB_XKB
		const xcb_query_extension_reply_t *reply =
			xcb_get_extension_data(x11->xcb_conn, &xcb_xkb_id);
		if (reply != NULL && reply->present) {
			x11->xkb_base_event = reply->first_event;
			x11->xkb_base_error = reply->first_error;

			xcb_xkb_use_extension_cookie_t cookie = xcb_xkb_use_extension(
				x11->xcb_conn, XCB_XKB_MAJOR_VERSION, XCB_XKB_MINOR_VERSION);
			xcb_xkb_use_extension_reply_t *reply =
				xcb_xkb_use_extension_reply(x11->xcb_conn, cookie, NULL);
			if (reply != NULL && reply->supported) {
				x11->xkb_supported = true;

				xcb_xkb_select_events(x11->xcb_conn,
					XCB_XKB_ID_USE_CORE_KBD,
					XCB_XKB_EVENT_TYPE_STATE_NOTIFY,
					0,
					XCB_XKB_EVENT_TYPE_STATE_NOTIFY,
					0,
					0,
					0);

				free(reply);
			}
		}
#endif

	wlr_signal_emit_safe(&x11->backend.events.new_input, &x11->keyboard_dev);

	for (size_t i = 0; i < x11->requested_outputs; ++i) {
		wlr_x11_output_create(&x11->backend);
	}

	return true;
}

static void backend_destroy(struct wlr_backend *backend) {
	if (!backend) {
		return;
	}

	struct wlr_x11_backend *x11 = get_x11_backend_from_backend(backend);

	struct wlr_x11_output *output, *tmp;
	wl_list_for_each_safe(output, tmp, &x11->outputs, link) {
		wlr_output_destroy(&output->wlr_output);
	}

	wlr_input_device_destroy(&x11->keyboard_dev);

	wlr_signal_emit_safe(&backend->events.destroy, backend);

	if (x11->event_source) {
		wl_event_source_remove(x11->event_source);
	}
	wl_list_remove(&x11->display_destroy.link);

	wlr_renderer_destroy(x11->renderer);

	if (x11->cursor) {
		xcb_free_cursor(x11->xcb_conn, x11->cursor);
	}
	if (x11->xlib_conn) {
		XCloseDisplay(x11->xlib_conn);
	}
	free(x11);
}

static struct wlr_renderer *backend_get_renderer(
		struct wlr_backend *backend) {
	struct wlr_x11_backend *x11 = get_x11_backend_from_backend(backend);
	return x11->renderer;
}

static bool backend_init_egl(struct wlr_backend *backend, struct wlr_egl *egl) {
	struct wlr_x11_backend *x11 = (struct wlr_x11_backend *)backend;
	return wlr_egl_init(egl, EGL_PLATFORM_X11_KHR,
		x11->xlib_conn, NULL, x11->screen->root_visual);
}

static const struct wlr_backend_impl backend_impl = {
	.start = backend_start,
	.destroy = backend_destroy,
	.get_renderer = backend_get_renderer,
	.init_egl = backend_init_egl,
};

bool wlr_backend_is_x11(struct wlr_backend *backend) {
	return backend->impl == &backend_impl;
}

static void handle_display_destroy(struct wl_listener *listener, void *data) {
	struct wlr_x11_backend *x11 =
		wl_container_of(listener, x11, display_destroy);
	backend_destroy(&x11->backend);
}

struct wlr_backend *wlr_x11_backend_create(struct wl_display *display,
		const char *x11_display,
		wlr_renderer_create_func_t create_renderer_func) {
	struct wlr_x11_backend *x11 = calloc(1, sizeof(*x11));
	if (!x11) {
		return NULL;
	}

	wlr_backend_init(&x11->backend, &backend_impl);
	x11->wl_display = display;
	wl_list_init(&x11->outputs);

	x11->xlib_conn = XOpenDisplay(x11_display);
	if (!x11->xlib_conn) {
		wlr_log(WLR_ERROR, "Failed to open X connection");
		goto error_x11;
	}

	x11->xcb_conn = XGetXCBConnection(x11->xlib_conn);
	if (!x11->xcb_conn || xcb_connection_has_error(x11->xcb_conn)) {
		wlr_log(WLR_ERROR, "Failed to open xcb connection");
		goto error_display;
	}

	XSetEventQueueOwner(x11->xlib_conn, XCBOwnsEventQueue);

	int fd = xcb_get_file_descriptor(x11->xcb_conn);
	struct wl_event_loop *ev = wl_display_get_event_loop(display);
	uint32_t events = WL_EVENT_READABLE | WL_EVENT_ERROR | WL_EVENT_HANGUP;
	x11->event_source = wl_event_loop_add_fd(ev, fd, events, x11_event, x11);
	if (!x11->event_source) {
		wlr_log(WLR_ERROR, "Could not create event source");
		goto error_display;
	}
	wl_event_source_check(x11->event_source);

	x11->screen = xcb_setup_roots_iterator(xcb_get_setup(x11->xcb_conn)).data;

	if (!create_renderer_func) {
		create_renderer_func = wlr_renderer_autocreate;
	}

	x11->renderer = create_renderer_func(&x11->backend);

	if (x11->renderer == NULL) {
		wlr_log(WLR_ERROR, "Failed to create renderer");
		goto error_event;
	}

	wlr_input_device_init(&x11->keyboard_dev, WLR_INPUT_DEVICE_KEYBOARD,
		&input_device_impl, "X11 keyboard", 0, 0);
	wlr_keyboard_init(&x11->keyboard, &keyboard_impl);
	x11->keyboard_dev.keyboard = &x11->keyboard;

	x11->display_destroy.notify = handle_display_destroy;
	wl_display_add_destroy_listener(display, &x11->display_destroy);

	return &x11->backend;

error_event:
	wl_event_source_remove(x11->event_source);
error_display:
	XCloseDisplay(x11->xlib_conn);
error_x11:
	free(x11);
	return NULL;
}
