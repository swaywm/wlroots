#define _POSIX_C_SOURCE 200809L

#include <GLES2/gl2.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wayland-egl.h>
#include <wlr/render/egl.h>
#include "xdg-shell-client-protocol.h"
#include "input-timestamps-unstable-v1-client-protocol.h"

/**
 * global state of the client
 */

struct egl_info {
	struct wlr_egl *egl;

	struct wl_egl_window *egl_window;
	struct wlr_egl_surface *egl_surface;
	struct wl_surface *surface;

	uint32_t width;
	uint32_t height;

	struct wl_callback *frame_callback;
};

struct client {
	struct egl_info *egl_info;

	struct wl_pointer *pointer;
	struct wl_keyboard *keyboard;

	struct zwp_input_timestamps_v1 *pointer_input_timestamps;
	struct zwp_input_timestamps_v1 *keyboard_input_timestamps;

	struct timespec pointer_timestamp;
	struct timespec keyboard_timestamp;
};

/**
 * global declarations
 */

static struct wl_shm *shm = NULL;
static struct wl_compositor *compositor = NULL;
static struct wl_seat *seat = NULL;
static struct xdg_wm_base *xdg_wm_base = NULL;
static struct zwp_input_timestamps_manager_v1 *input_timestamps_manager = NULL;

static const struct wl_registry_listener registry_listener;
static const struct wl_seat_listener seat_listener;
static const struct wl_pointer_listener pointer_listener;
static const struct wl_keyboard_listener keyboard_listener;
static const struct xdg_surface_listener xdg_surface_listener;
static const struct xdg_toplevel_listener xdg_toplevel_listener;
static const struct zwp_input_timestamps_v1_listener input_timestamps_listener;

const int WIDTH = 100;
const int HEIGHT = 100;

struct timespec cur, cur_rep;
void update_current_time(void);
void print_time_and_latency(uint32_t, struct timespec);
void paint_pixels(struct egl_info *);

/**
 * registry and globals handling
 */

static void registry_handle_global(void *data, struct wl_registry *registry,
		uint32_t name, const char *interface, uint32_t version) {
	if (strcmp(interface, wl_shm_interface.name) == 0) {
		shm = wl_registry_bind(registry, name,
			&wl_shm_interface, version);
	} else if (strcmp(interface, wl_compositor_interface.name) == 0) {
		compositor = wl_registry_bind(registry, name,
			&wl_compositor_interface, version);
	} else if (strcmp(interface, wl_seat_interface.name) == 0) {
		seat = wl_registry_bind(registry, name,
			&wl_seat_interface, version);
		wl_seat_add_listener(seat, &seat_listener, data);
	} else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
		xdg_wm_base = wl_registry_bind(registry, name,
			&xdg_wm_base_interface, version);
	} else if (strcmp(interface, zwp_input_timestamps_manager_v1_interface.name) == 0) {
		input_timestamps_manager = wl_registry_bind(registry, name,
			&zwp_input_timestamps_manager_v1_interface, version);
	}
}

static void registry_handle_global_remove(void *data, struct wl_registry *registry,
		uint32_t time) {
	/* This space intentionally left blank */
}

static const struct wl_registry_listener registry_listener = {
	.global = registry_handle_global,
	.global_remove = registry_handle_global_remove,
};

/**
 * wl_seat handling
 */

static void seat_handle_capabilities(void *data, struct wl_seat *seat,
		enum wl_seat_capability caps) {
	struct client *c = (struct client *) data;

	/* Setup the pointer */

	if ((caps & WL_SEAT_CAPABILITY_POINTER)) {
		struct wl_pointer *pointer = wl_seat_get_pointer(seat);
		wl_pointer_add_listener(pointer, &pointer_listener, c);

		c->pointer = pointer;
	}

	/* Setup the keyboard */

	if ((caps & WL_SEAT_CAPABILITY_KEYBOARD)) {
		struct wl_keyboard *keyboard = wl_seat_get_keyboard(seat);
		wl_keyboard_add_listener(keyboard, &keyboard_listener, c);

		c->keyboard = keyboard;
	}
}

static void seat_handle_name(void *data, struct wl_seat *seat,
		const char *name) {
	/* This space intentionally left blank */
}

static const struct wl_seat_listener seat_listener = {
	.capabilities = seat_handle_capabilities,
	.name = seat_handle_name,
};

/**
 * wl_pointer handling
 *
 * It simply prints the relevant pointer events (button press, motion, axis
 * movement) along with the associated timestamps, both coarse and fine
 * (through the input-timestamps object).
 */

static void pointer_handle_button(void *data, struct wl_pointer *pointer,
		uint32_t serial, uint32_t time, uint32_t button, uint32_t state) {
	update_current_time();

	if (state == WL_POINTER_BUTTON_STATE_PRESSED) {
		fprintf(stdout, "POINTER: Button press %"PRIu32" (", button);
		print_time_and_latency(time, ((struct client *) data)->pointer_timestamp);
		fprintf(stdout, ")\n");
	}
}

static void pointer_handle_enter(void *data, struct wl_pointer *wl_pointer,
		uint32_t serial, struct wl_surface *surface,
		wl_fixed_t surface_x, wl_fixed_t surface_y) {
	/* This space intentionally left blank */
}

static void pointer_handle_leave(void *data, struct wl_pointer *wl_pointer,
		uint32_t serial, struct wl_surface *surface) {
	/* This space intentionally left blank */
}

static void pointer_handle_motion(void *data, struct wl_pointer *wl_pointer,
		uint32_t time, wl_fixed_t surface_x, wl_fixed_t surface_y) {
	update_current_time();

	fprintf(stdout, "POINTER: Motion to (%f,%f) (",
		wl_fixed_to_double(surface_x), wl_fixed_to_double(surface_y));
	print_time_and_latency(time, ((struct client *) data)->pointer_timestamp);
	fprintf(stdout, ")\n");
}

static void pointer_handle_axis(void *data, struct wl_pointer *wl_pointer,
		uint32_t time, uint32_t axis, wl_fixed_t value) {
	update_current_time();

	fprintf(stdout, "POINTER: Axis %"PRIu32" by vector %f (",
		axis, wl_fixed_to_double(value));
	print_time_and_latency(time, ((struct client *) data)->pointer_timestamp);
	fprintf(stdout, ")\n");
}

static void pointer_handle_frame(void *data, struct wl_pointer *wl_pointer) {
	/* This space intentionally left blank */
}

static void pointer_handle_axis_source(void *data,
		struct wl_pointer *wl_pointer, uint32_t axis_source) {
	/* This space intentionally left blank */
}

static void pointer_handle_axis_stop(void *data,
		struct wl_pointer *wl_pointer, uint32_t time, uint32_t axis) {
	update_current_time();

	fprintf(stdout, "POINTER: Axis %"PRIu32" stopped (", axis);
	print_time_and_latency(time, ((struct client *) data)->pointer_timestamp);
	fprintf(stdout, ")\n");
}

static void pointer_handle_axis_discrete(void *data,
		struct wl_pointer *wl_pointer, uint32_t axis, int32_t discrete) {
	/* This space intentionally left blank */
}

static const struct wl_pointer_listener pointer_listener = {
	.enter = pointer_handle_enter,
	.leave = pointer_handle_leave,
	.motion = pointer_handle_motion,
	.button = pointer_handle_button,
	.axis = pointer_handle_axis,
	.frame = pointer_handle_frame,
	.axis_source = pointer_handle_axis_source,
	.axis_stop = pointer_handle_axis_stop,
	.axis_discrete = pointer_handle_axis_discrete,
};

/**
 * wl_keyboard handling
 *
 * It simply prints the keypress events along with the associated timestamps,
 * both coarse and fine (through the input-timestamps object).
 */

static void keyboard_handle_keymap(void *data, struct wl_keyboard *wl_keyboard,
		uint32_t format, int32_t fd, uint32_t size) {
	/* This space intentionally left blank */
}

static void keyboard_handle_enter(void *data, struct wl_keyboard *wl_keyboard,
		uint32_t serial, struct wl_surface *surface, struct wl_array *keys) {
	/* This space intentionally left blank */
}

static void keyboard_handle_leave(void *data, struct wl_keyboard *wl_keyboard,
		uint32_t serial, struct wl_surface *surface) {
	/* This space intentionally left blank */
}

static void keyboard_handle_key(void *data, struct wl_keyboard *wl_keyboard,
		uint32_t serial, uint32_t time, uint32_t key, uint32_t state) {
	update_current_time();

	if (state == WL_KEYBOARD_KEY_STATE_PRESSED) {
		fprintf(stdout, "KEYBOARD: Key press %"PRIu32" (", key);
		print_time_and_latency(time, ((struct client *) data)->keyboard_timestamp);
		fprintf(stdout, ")\n");
	}
}

static void keyboard_handle_modifiers(void *data, struct wl_keyboard *wl_keyboard,
		uint32_t serial, uint32_t mods_depressed, uint32_t mods_latched,
		uint32_t mods_locked, uint32_t group) {
	/* This space intentionally left blank */
}

static void keyboard_handle_repeat_info(void *data, struct wl_keyboard *wl_keyboard,
		int32_t rate, int32_t delay) {
	/* This space intentionally left blank */
}

static const struct wl_keyboard_listener keyboard_listener = {
	.keymap = keyboard_handle_keymap,
	.enter = keyboard_handle_enter,
	.leave = keyboard_handle_leave,
	.key = keyboard_handle_key,
	.modifiers = keyboard_handle_modifiers,
	.repeat_info = keyboard_handle_repeat_info,
};

/**
 * xdg_surface handling
 */

static void xdg_surface_handle_configure(void *data,
		struct xdg_surface *xdg_surface, uint32_t serial) {
	xdg_surface_ack_configure(xdg_surface, serial);

	struct egl_info *e = data;
	wl_egl_window_resize(e->egl_window, e->width, e->height, 0, 0);
	paint_pixels(e);
}

static const struct xdg_surface_listener xdg_surface_listener = {
	.configure = xdg_surface_handle_configure,
};

/**
 * xdg_toplevel handling
 */

static void xdg_toplevel_handle_configure(void *data,
		struct xdg_toplevel *xdg_toplevel, int32_t width, int32_t height,
		struct wl_array *states) {
	struct egl_info *e = data;

	if (width == 0 && height == 0) {
		return;
	}
	e->width = width;
	e->height = height;
}

static void xdg_toplevel_handle_close(void *data,
		struct xdg_toplevel *xdg_toplevel) {
	struct egl_info *e = data;
	wlr_egl_finish(e->egl);

	exit(EXIT_SUCCESS);
}

static const struct xdg_toplevel_listener xdg_toplevel_listener = {
	.configure = xdg_toplevel_handle_configure,
	.close = xdg_toplevel_handle_close,
};

/**
 * input_timestamps handling
 *
 * We simply store the received timestamp in the timespec struct pointed to by
 * the user data.
 */

static void input_timestamps_handle_timestamp(void *data,
		struct zwp_input_timestamps_v1 *zwp_input_timestamps_v1,
		uint32_t tv_sec_hi, uint32_t tv_sec_lo, uint32_t tv_nsec) {
	struct timespec *timestamp = data;

	timestamp->tv_sec = ((uint64_t) tv_sec_hi << 32) + tv_sec_lo;
	timestamp->tv_nsec = tv_nsec;
}

static const struct zwp_input_timestamps_v1_listener input_timestamps_listener = {
	.timestamp = input_timestamps_handle_timestamp,
};

/**
 * helper functions:
 *
 * paint_pixels():
 * Sets all pixels of the EGL surface to a fixed color.
 *
 * update_current_time():
 * Updates the global clocks with two samples.
 *
 * print_time_and_latency():
 * Prints the input event timestamps, along with estimated latency measured by
 * the previous function.
 */

inline void paint_pixels(struct egl_info *e) {
	eglMakeCurrent(e->egl->display, e->egl_surface, e->egl_surface,
		e->egl->context);
	glViewport(0, 0, e->width, e->height);

	glClearColor(1.0f, 1.0f, 1.0f, 1.0f); /* white */
	glClear(GL_COLOR_BUFFER_BIT);

	eglSwapBuffers(e->egl->display, e->egl_surface);
}

inline void update_current_time(void) {
	clock_gettime(CLOCK_MONOTONIC, &cur);
	clock_gettime(CLOCK_MONOTONIC, &cur_rep);
}

inline void print_time_and_latency(uint32_t event_time_msec, struct timespec protocol_time) {
		fprintf(stdout, "time: %"PRIu32" ms (event), %ld%09ld ns (protocol); ",
			event_time_msec, protocol_time.tv_sec, protocol_time.tv_nsec);

	if (event_time_msec/1000 == protocol_time.tv_sec) {
		double input_time, first_sample, second_sample;
		input_time = protocol_time.tv_sec + ((double) protocol_time.tv_nsec)/1000000000;
		first_sample = cur.tv_sec + ((double) cur.tv_nsec)/1000000000;
		second_sample = cur_rep.tv_sec + ((double) cur_rep.tv_nsec)/1000000000;
		assert((second_sample > first_sample) && (first_sample > input_time));
		fprintf(stdout, "latency: %f ms",
			1000 * (first_sample - input_time - (second_sample - first_sample)));
	} else {
		fprintf(stdout, "latency: error!");
	}
}

/**
 * input-timestamps:
 *
 * This client serves as an example for the input-timestamps-unstable-v1
 * protocol.
 *
 * The client requests a input timestamps object for the keyboard and the
 * pointer -- if possible -- and prints all timed events as they come. Future
 * directions could include capturing touch events, and toggling reporting for
 * specific input devices via command line flags.
 *
 * We need a minimal toplevel to grab focus to receive input events. We use EGL
 * (with helper functions from wlroots) to display a solid colored rectangle as
 * an xdg toplevel.
 *
 */

int main(int argc, char **argv) {

	/* Connect to the display */

	struct wl_display *display = wl_display_connect(NULL);
	if (display == NULL) {
		fprintf(stderr, "Could not connect to a Wayland display\n");
		return EXIT_FAILURE;
	}

	/* Setup global state */

	struct client *c = calloc(1, sizeof(struct client));

	/* Get the registry and set listeners */

	struct wl_registry *registry = wl_display_get_registry(display);
	wl_registry_add_listener(registry, &registry_listener, c);
	wl_display_dispatch(display);
	wl_display_roundtrip(display);

	/* Check that all the global interfaces were captured */

	if (shm == NULL) {
		fprintf(stderr, "wl_shm not available\n");
		return EXIT_FAILURE;
	}
	if (compositor == NULL) {
		fprintf(stderr, "wl_compositor not available\n");
		return EXIT_FAILURE;
	}
	if (seat == NULL) {
		fprintf(stderr, "wl_seat not available\n");
		return EXIT_FAILURE;
	}
	if (input_timestamps_manager == NULL) {
		fprintf(stderr, "zwp_input_timestamps_unstable_manager_v1 not available\n");
		return EXIT_FAILURE;
	}

	/* Initialize EGL context */

	struct egl_info *e = calloc(1, sizeof(struct egl_info));
	e->egl = calloc(1, sizeof(struct wlr_egl));
	e->width = WIDTH;
	e->height = HEIGHT;

	wlr_egl_init(e->egl, EGL_PLATFORM_WAYLAND_EXT, display, NULL,
		WL_SHM_FORMAT_ARGB8888);

	c->egl_info = e;

	/* Create the surface and xdg_toplevels, and set listeners */

	struct wl_surface *surface = wl_compositor_create_surface(compositor);
	struct xdg_surface *xdg_surface =
		xdg_wm_base_get_xdg_surface(xdg_wm_base, surface);
	struct xdg_toplevel *xdg_toplevel = xdg_surface_get_toplevel(xdg_surface);

	xdg_surface_add_listener(xdg_surface, &xdg_surface_listener, e);
	xdg_toplevel_add_listener(xdg_toplevel, &xdg_toplevel_listener, e);

	/* Create the EGL window and surface */

	wl_surface_commit(surface);

	e->egl_window = wl_egl_window_create(surface, e->width, e->height);
	e->egl_surface = wlr_egl_create_surface(e->egl, e->egl_window);
	e->surface = surface;

	wl_display_roundtrip(display);

	paint_pixels(e);

	/* Get the input timestamps and destroy manager */

	if (c->pointer) {
		c->pointer_input_timestamps =
			zwp_input_timestamps_manager_v1_get_pointer_timestamps(
				input_timestamps_manager, c->pointer);

		zwp_input_timestamps_v1_add_listener(c->pointer_input_timestamps,
			&input_timestamps_listener, &c->pointer_timestamp);
	}

	if (c->keyboard) {
		c->keyboard_input_timestamps =
			zwp_input_timestamps_manager_v1_get_keyboard_timestamps(
				input_timestamps_manager, c->keyboard);

		zwp_input_timestamps_v1_add_listener(c->keyboard_input_timestamps,
			&input_timestamps_listener, &c->keyboard_timestamp);
	}

	zwp_input_timestamps_manager_v1_destroy(input_timestamps_manager);

	/* Run display */

	while (wl_display_dispatch(display) != -1) {
		/* This space intentionally left blank */
	}

	return EXIT_SUCCESS;
}
