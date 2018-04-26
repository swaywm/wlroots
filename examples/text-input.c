#include <GLES2/gl2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wayland-client.h>
#include <wayland-egl.h>
#include <wlr/render/egl.h>
#include "text-input-unstable-v3-client-protocol.h"
#include "xdg-shell-client-protocol.h"

#include <linux/input-event-codes.h>

/**
 * Usage: idle-inhibit
 * Creates a xdg-toplevel using the idle-inhibit protocol.
 * It will be solid green, when it has an idle inhibitor, and solid yellow if
 * it does not.
 * Left click with a pointer will toggle this state. (Touch is not supported
 * for now).
 */

static int sleeptime = 0;

static int width = 100, height = 200;
static int enabled = 0;

static struct wl_display *display = NULL;
static struct wl_compositor *compositor = NULL;
static struct wl_seat *seat = NULL;
static struct xdg_wm_base *wm_base = NULL;
static struct zwp_text_input_manager_v3 *text_input_manager = NULL;
static struct zwp_text_input_v3 *text_input	= NULL;

struct wlr_egl egl;
struct wl_egl_window *egl_window;
struct wlr_egl_surface *egl_surface;

static void draw(void) {
	eglMakeCurrent(egl.display, egl_surface, egl_surface, egl.context);

	float color[] = {1.0, 1.0, 0.0, 1.0};
	color[0] = enabled / 2.0;

	glViewport(0, 0, width, height);
	glClearColor(color[0], color[1], color[2], 1.0);
	glClear(GL_COLOR_BUFFER_BIT);

	eglSwapBuffers(egl.display, egl_surface);
}

static void noop() {}

void text_input_handle_enter(void *data,
		struct zwp_text_input_v3 *zwp_text_input_v3,
		struct wl_surface *surface) {
	enabled = 1;
	draw();
	zwp_text_input_v3_enable(zwp_text_input_v3, 0);
}

void text_input_handle_leave(void *data,
		struct zwp_text_input_v3 *zwp_text_input_v3,
		struct wl_surface *surface) {
	enabled = 2;
	draw();
	wl_display_roundtrip(display);
	sleep(sleeptime);
	zwp_text_input_v3_disable(zwp_text_input_v3);
	enabled = 0;
	draw();
}

static const struct zwp_text_input_v3_listener text_input_listener = {
	.enter = text_input_handle_enter,
	.leave = text_input_handle_leave,
	.commit_string = noop,
	.delete_surrounding_text = noop,
	.preedit_string = noop,
};

static void xdg_surface_handle_configure(void *data,
		struct xdg_surface *xdg_surface, uint32_t serial) {
	xdg_surface_ack_configure(xdg_surface, serial);
	wl_egl_window_resize(egl_window, width, height, 0, 0);
	draw();
}

static const struct xdg_surface_listener xdg_surface_listener = {
	.configure = xdg_surface_handle_configure,
};

static void xdg_toplevel_handle_configure(void *data,
		struct xdg_toplevel *xdg_toplevel, int32_t w, int32_t h,
		struct wl_array *states) {
	width = w;
	height = h;
}

static void xdg_toplevel_handle_close(void *data,
		struct xdg_toplevel *xdg_toplevel) {
	exit(EXIT_SUCCESS);
}

static const struct xdg_toplevel_listener xdg_toplevel_listener = {
	.configure = xdg_toplevel_handle_configure,
	.close = xdg_toplevel_handle_close,
};

static void handle_global(void *data, struct wl_registry *registry,
		uint32_t name, const char *interface, uint32_t version) {
	if (strcmp(interface, "wl_compositor") == 0) {
		compositor = wl_registry_bind(registry, name,
			&wl_compositor_interface, 1);
	} else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
		wm_base = wl_registry_bind(registry, name, &xdg_wm_base_interface, 1);
	} else if (strcmp(interface, zwp_text_input_manager_v3_interface.name) == 0) {
		text_input_manager = wl_registry_bind(registry, name,
			&zwp_text_input_manager_v3_interface, 1);

	} else if (strcmp(interface, wl_seat_interface.name) == 0) {
		seat = wl_registry_bind(registry, name, &wl_seat_interface, version);
	}
}

static void handle_global_remove(void *data, struct wl_registry *registry,
		uint32_t name) {
	// who cares
}

static const struct wl_registry_listener registry_listener = {
	.global = handle_global,
	.global_remove = handle_global_remove,
};

int main(int argc, char **argv) {
	if (argc > 1) {
		sleeptime = atoi(argv[1]);
		width = 200;
		height = 100;
	}
	display = wl_display_connect(NULL);
	if (display == NULL) {
		fprintf(stderr, "Failed to create display\n");
		return EXIT_FAILURE;
	}

	struct wl_registry *registry = wl_display_get_registry(display);
	wl_registry_add_listener(registry, &registry_listener, NULL);
	wl_display_dispatch(display);
	wl_display_roundtrip(display);

	if (compositor == NULL) {
		fprintf(stderr, "wl-compositor not available\n");
		return EXIT_FAILURE;
	}
	if (wm_base == NULL) {
		fprintf(stderr, "xdg-shell not available\n");
		return EXIT_FAILURE;
	}
	if (text_input_manager == NULL) {
		fprintf(stderr, "text-input not available\n");
		return EXIT_FAILURE;
	}

	text_input = zwp_text_input_manager_v3_get_text_input(text_input_manager, seat);

	zwp_text_input_v3_add_listener(text_input, &text_input_listener, NULL);


	wlr_egl_init(&egl, EGL_PLATFORM_WAYLAND_EXT, display, NULL,
		WL_SHM_FORMAT_ARGB8888);

	struct wl_surface *surface = wl_compositor_create_surface(compositor);
	struct xdg_surface *xdg_surface =
		xdg_wm_base_get_xdg_surface(wm_base, surface);
	struct xdg_toplevel *xdg_toplevel = xdg_surface_get_toplevel(xdg_surface);

	xdg_surface_add_listener(xdg_surface, &xdg_surface_listener, NULL);
	xdg_toplevel_add_listener(xdg_toplevel, &xdg_toplevel_listener, NULL);

	wl_surface_commit(surface);

	egl_window = wl_egl_window_create(surface, width, height);
	egl_surface = wlr_egl_create_surface(&egl, egl_window);

	wl_display_roundtrip(display);

	draw();

	while (wl_display_dispatch(display) != -1) {
		// This space intentionally left blank
	}

	return EXIT_SUCCESS;
}
