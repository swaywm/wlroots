#define _POSIX_C_SOURCE 200112L

#include <assert.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include <wlr/config.h>

#include <drm_fourcc.h>
#include <X11/Xlib-xcb.h>
#include <wayland-server-core.h>
#include <xcb/xcb.h>
#include <xcb/dri3.h>
#include <xcb/present.h>
#include <xcb/xfixes.h>
#include <xcb/xinput.h>

#include <wlr/backend/interface.h>
#include <wlr/backend/x11.h>
#include <wlr/interfaces/wlr_input_device.h>
#include <wlr/interfaces/wlr_keyboard.h>
#include <wlr/interfaces/wlr_pointer.h>
#include <wlr/render/egl.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/util/log.h>

#include "backend/x11.h"
#include "render/drm_format_set.h"
#include "render/gbm_allocator.h"
#include "render/wlr_renderer.h"
#include "util/signal.h"

// See dri2_format_for_depth in mesa
const struct wlr_x11_format formats[] = {
	{ .drm = DRM_FORMAT_XRGB8888, .depth = 24, .bpp = 32 },
	{ .drm = DRM_FORMAT_ARGB8888, .depth = 32, .bpp = 32 },
};

static const struct wlr_x11_format *x11_format_from_depth(uint8_t depth) {
	for (size_t i = 0; i < sizeof(formats) / sizeof(formats[0]); i++) {
		if (formats[i].depth == depth) {
			return &formats[i];
		}
	}
	return NULL;
}

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

static void handle_x11_error(struct wlr_x11_backend *x11, xcb_value_error_t *ev);
static void handle_x11_unknown_event(struct wlr_x11_backend *x11,
	xcb_generic_event_t *ev);

static void handle_x11_event(struct wlr_x11_backend *x11,
		xcb_generic_event_t *event) {
	switch (event->response_type & XCB_EVENT_RESPONSE_TYPE_MASK) {
	case XCB_EXPOSE: {
		xcb_expose_event_t *ev = (xcb_expose_event_t *)event;
		struct wlr_x11_output *output =
			get_x11_output_from_window_id(x11, ev->window);
		if (output != NULL) {
			wlr_output_update_needs_frame(&output->wlr_output);
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
		} else {
			wlr_log(WLR_DEBUG, "Unhandled client message %"PRIu32,
				ev->data.data32[0]);
		}
		break;
	}
	case XCB_GE_GENERIC: {
		xcb_ge_generic_event_t *ev = (xcb_ge_generic_event_t *)event;
		if (ev->extension == x11->xinput_opcode) {
			handle_x11_xinput_event(x11, ev);
		} else if (ev->extension == x11->present_opcode) {
			handle_x11_present_event(x11, ev);
		} else {
			handle_x11_unknown_event(x11, event);
		}
		break;
	}
	case 0: {
		xcb_value_error_t *ev = (xcb_value_error_t *)event;
		handle_x11_error(x11, ev);
		break;
	}
	default:
		handle_x11_unknown_event(x11, event);
		break;
	}
}

static int x11_event(int fd, uint32_t mask, void *data) {
	struct wlr_x11_backend *x11 = data;

	if ((mask & WL_EVENT_HANGUP) || (mask & WL_EVENT_ERROR)) {
		wl_display_terminate(x11->wl_display);
		return 0;
	}

	xcb_generic_event_t *e;
	while ((e = xcb_poll_for_event(x11->xcb))) {
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
	wlr_egl_finish(&x11->egl);
	free(x11->drm_format);

#if WLR_HAS_XCB_ERRORS
	xcb_errors_context_free(x11->errors_context);
#endif

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

static const struct wlr_backend_impl backend_impl = {
	.start = backend_start,
	.destroy = backend_destroy,
	.get_renderer = backend_get_renderer,
};

bool wlr_backend_is_x11(struct wlr_backend *backend) {
	return backend->impl == &backend_impl;
}

static void handle_display_destroy(struct wl_listener *listener, void *data) {
	struct wlr_x11_backend *x11 =
		wl_container_of(listener, x11, display_destroy);
	backend_destroy(&x11->backend);
}

static xcb_depth_t *get_depth(xcb_screen_t *screen, uint8_t depth) {
	xcb_depth_iterator_t iter = xcb_screen_allowed_depths_iterator(screen);
	while (iter.rem > 0) {
		if (iter.data->depth == depth) {
			return iter.data;
		}
		xcb_depth_next(&iter);
	}
	return NULL;
}

static xcb_visualid_t pick_visualid(xcb_depth_t *depth) {
	xcb_visualtype_t *visuals = xcb_depth_visuals(depth);
	for (int i = 0; i < xcb_depth_visuals_length(depth); i++) {
		if (visuals[i]._class == XCB_VISUAL_CLASS_TRUE_COLOR) {
			return visuals[i].visual_id;
		}
	}
	return 0;
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

	x11->xcb = XGetXCBConnection(x11->xlib_conn);
	if (!x11->xcb || xcb_connection_has_error(x11->xcb)) {
		wlr_log(WLR_ERROR, "Failed to open xcb connection");
		goto error_display;
	}

	XSetEventQueueOwner(x11->xlib_conn, XCBOwnsEventQueue);

	struct {
		const char *name;
		xcb_intern_atom_cookie_t cookie;
		xcb_atom_t *atom;
	} atom[] = {
		{ .name = "WM_PROTOCOLS", .atom = &x11->atoms.wm_protocols },
		{ .name = "WM_DELETE_WINDOW", .atom = &x11->atoms.wm_delete_window },
		{ .name = "_NET_WM_NAME", .atom = &x11->atoms.net_wm_name },
		{ .name = "UTF8_STRING", .atom = &x11->atoms.utf8_string },
		{ .name = "_VARIABLE_REFRESH", .atom = &x11->atoms.variable_refresh },
	};

	for (size_t i = 0; i < sizeof(atom) / sizeof(atom[0]); ++i) {
		atom[i].cookie = xcb_intern_atom(x11->xcb,
			true, strlen(atom[i].name), atom[i].name);
	}

	for (size_t i = 0; i < sizeof(atom) / sizeof(atom[0]); ++i) {
		xcb_intern_atom_reply_t *reply = xcb_intern_atom_reply(
			x11->xcb, atom[i].cookie, NULL);

		if (reply) {
			*atom[i].atom = reply->atom;
			free(reply);
		} else {
			*atom[i].atom = XCB_ATOM_NONE;
		}
	}

	const xcb_query_extension_reply_t *ext;

	// DRI3 extension

	ext = xcb_get_extension_data(x11->xcb, &xcb_dri3_id);
	if (!ext || !ext->present) {
		wlr_log(WLR_ERROR, "X11 does not support DRI3 extension");
		goto error_display;
	}

	xcb_dri3_query_version_cookie_t dri3_cookie =
		xcb_dri3_query_version(x11->xcb, 1, 2);
	xcb_dri3_query_version_reply_t *dri3_reply =
		xcb_dri3_query_version_reply(x11->xcb, dri3_cookie, NULL);
	if (!dri3_reply || dri3_reply->major_version < 1 ||
			dri3_reply->minor_version < 2) {
		wlr_log(WLR_ERROR, "X11 does not support required DRI3 version");
		goto error_display;
	}
	free(dri3_reply);

	// Present extension

	ext = xcb_get_extension_data(x11->xcb, &xcb_present_id);
	if (!ext || !ext->present) {
		wlr_log(WLR_ERROR, "X11 does not support Present extension");
		goto error_display;
	}
	x11->present_opcode = ext->major_opcode;

	xcb_present_query_version_cookie_t present_cookie =
		xcb_present_query_version(x11->xcb, 1, 2);
	xcb_present_query_version_reply_t *present_reply =
		xcb_present_query_version_reply(x11->xcb, present_cookie, NULL);
	if (!present_reply || present_reply->major_version < 1) {
		wlr_log(WLR_ERROR, "X11 does not support required Present version");
		free(present_reply);
		goto error_display;
	}
	free(present_reply);

	// Xfixes extension

	ext = xcb_get_extension_data(x11->xcb, &xcb_xfixes_id);
	if (!ext || !ext->present) {
		wlr_log(WLR_ERROR, "X11 does not support Xfixes extension");
		goto error_display;
	}

	xcb_xfixes_query_version_cookie_t fixes_cookie =
		xcb_xfixes_query_version(x11->xcb, 4, 0);
	xcb_xfixes_query_version_reply_t *fixes_reply =
		xcb_xfixes_query_version_reply(x11->xcb, fixes_cookie, NULL);

	if (!fixes_reply || fixes_reply->major_version < 4) {
		wlr_log(WLR_ERROR, "X11 does not support required Xfixes version");
		free(fixes_reply);
		goto error_display;
	}
	free(fixes_reply);

	// Xinput extension

	ext = xcb_get_extension_data(x11->xcb, &xcb_input_id);
	if (!ext || !ext->present) {
		wlr_log(WLR_ERROR, "X11 does not support Xinput extension");
		goto error_display;
	}
	x11->xinput_opcode = ext->major_opcode;

	xcb_input_xi_query_version_cookie_t xi_cookie =
		xcb_input_xi_query_version(x11->xcb, 2, 0);
	xcb_input_xi_query_version_reply_t *xi_reply =
		xcb_input_xi_query_version_reply(x11->xcb, xi_cookie, NULL);

	if (!xi_reply || xi_reply->major_version < 2) {
		wlr_log(WLR_ERROR, "X11 does not support required Xinput version");
		free(xi_reply);
		goto error_display;
	}
	free(xi_reply);

	int fd = xcb_get_file_descriptor(x11->xcb);
	struct wl_event_loop *ev = wl_display_get_event_loop(display);
	uint32_t events = WL_EVENT_READABLE | WL_EVENT_ERROR | WL_EVENT_HANGUP;
	x11->event_source = wl_event_loop_add_fd(ev, fd, events, x11_event, x11);
	if (!x11->event_source) {
		wlr_log(WLR_ERROR, "Could not create event source");
		goto error_display;
	}
	wl_event_source_check(x11->event_source);

	x11->screen = xcb_setup_roots_iterator(xcb_get_setup(x11->xcb)).data;
	if (!x11->screen) {
		wlr_log(WLR_ERROR, "Failed to get X11 screen");
		goto error_display;
	}

	x11->depth = get_depth(x11->screen, 32);
	if (!x11->depth) {
		wlr_log(WLR_ERROR, "Failed to get 32-bit depth for X11 screen");
		goto error_display;
	}

	x11->visualid = pick_visualid(x11->depth);
	if (!x11->visualid) {
		wlr_log(WLR_ERROR, "Failed to pick X11 visual");
		goto error_display;
	}

	x11->x11_format = x11_format_from_depth(x11->depth->depth);
	if (!x11->x11_format) {
		wlr_log(WLR_ERROR, "Unsupported depth %"PRIu8, x11->depth->depth);
		goto error_display;
	}

	x11->colormap = xcb_generate_id(x11->xcb);
	xcb_create_colormap(x11->xcb, XCB_COLORMAP_ALLOC_NONE, x11->colormap,
		x11->screen->root, x11->visualid);

	if (!create_renderer_func) {
		create_renderer_func = wlr_renderer_autocreate;
	}

	static EGLint config_attribs[] = {
		EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
		EGL_RED_SIZE, 1,
		EGL_GREEN_SIZE, 1,
		EGL_BLUE_SIZE, 1,
		EGL_ALPHA_SIZE, 1,
		EGL_NONE,
	};

	x11->renderer = create_renderer_func(&x11->egl, EGL_PLATFORM_X11_KHR,
		x11->xlib_conn, config_attribs, x11->screen->root_visual);
	if (x11->renderer == NULL) {
		wlr_log(WLR_ERROR, "Failed to create renderer");
		goto error_event;
	}

	// TODO: we can use DRI3Open instead
	int drm_fd = wlr_renderer_get_drm_fd(x11->renderer);
	if (fd < 0) {
		wlr_log(WLR_ERROR, "Failed to get DRM device FD from renderer");
		return false;
	}

	drm_fd = dup(drm_fd);
	if (fd < 0) {
		wlr_log_errno(WLR_ERROR, "dup failed");
		return false;
	}

	struct wlr_gbm_allocator *alloc = wlr_gbm_allocator_create(drm_fd);
	if (alloc == NULL) {
		wlr_log(WLR_ERROR, "Failed to create GBM allocator");
		return false;
	}
	x11->allocator = &alloc->base;

	const struct wlr_drm_format_set *formats =
		wlr_renderer_get_dmabuf_render_formats(x11->renderer);
	if (formats == NULL) {
		wlr_log(WLR_ERROR, "Failed to get available DMA-BUF formats from renderer");
		return false;
	}
	const struct wlr_drm_format *format =
		wlr_drm_format_set_get(formats, x11->x11_format->drm);
	if (format == NULL) {
		wlr_log(WLR_ERROR, "Renderer doesn't support DRM format 0x%"PRIX32,
			x11->x11_format->drm);
		return false;
	}
	// TODO intersect modifiers with DRI3GetSupportedModifiers
	x11->drm_format = wlr_drm_format_dup(format);

#if WLR_HAS_XCB_ERRORS
	if (xcb_errors_context_new(x11->xcb, &x11->errors_context) != 0) {
		wlr_log(WLR_ERROR, "Failed to create error context");
		return false;
	}
#endif

	x11->present_event_id = xcb_generate_id(x11->xcb);

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

static void handle_x11_error(struct wlr_x11_backend *x11, xcb_value_error_t *ev) {
#if WLR_HAS_XCB_ERRORS
	const char *major_name = xcb_errors_get_name_for_major_code(
		x11->errors_context, ev->major_opcode);
	if (!major_name) {
		wlr_log(WLR_DEBUG, "X11 error happened, but could not get major name");
		goto log_raw;
	}

	const char *minor_name = xcb_errors_get_name_for_minor_code(
		x11->errors_context, ev->major_opcode, ev->minor_opcode);

	const char *extension;
	const char *error_name = xcb_errors_get_name_for_error(x11->errors_context,
		ev->error_code, &extension);
	if (!error_name) {
		wlr_log(WLR_DEBUG, "X11 error happened, but could not get error name");
		goto log_raw;
	}

	wlr_log(WLR_ERROR, "X11 error: op %s (%s), code %s (%s), "
		"sequence %"PRIu16", value %"PRIu32,
		major_name, minor_name ? minor_name : "no minor",
		error_name, extension ? extension : "no extension",
		ev->sequence, ev->bad_value);

	return;

log_raw:
#endif

	wlr_log(WLR_ERROR, "X11 error: op %"PRIu8":%"PRIu16", code %"PRIu8", "
		"sequence %"PRIu16", value %"PRIu32,
		ev->major_opcode, ev->minor_opcode, ev->error_code,
		ev->sequence, ev->bad_value);
}

static void handle_x11_unknown_event(struct wlr_x11_backend *x11,
		xcb_generic_event_t *ev) {
#if WLR_HAS_XCB_ERRORS
	const char *extension;
	const char *event_name = xcb_errors_get_name_for_xcb_event(
		x11->errors_context, ev, &extension);
	if (!event_name) {
		wlr_log(WLR_DEBUG, "No name for unhandled event: %u",
			ev->response_type);
		return;
	}

	wlr_log(WLR_DEBUG, "Unhandled X11 event: %s (%u)", event_name, ev->response_type);
#else
	wlr_log(WLR_DEBUG, "Unhandled X11 event: %u", ev->response_type);
#endif
}
