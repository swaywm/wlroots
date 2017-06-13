#ifndef _EXAMPLE_SHARED_H
#define _EXAMPLE_SHARED_H
#define _POSIX_C_SOURCE 199309L
#include <time.h>
#include <stdbool.h>
#include <xkbcommon/xkbcommon.h>
#include <wayland-server-protocol.h>
#include <wlr/backend.h>
#include <wlr/session.h>
#include <wlr/types.h>

struct output_state {
	struct compositor_state *compositor;
	struct wlr_output *output;
	struct wl_listener frame;
	struct timespec last_frame;
	struct wl_list link;
	void *data;
};

struct keyboard_state {
	struct compositor_state *compositor;
	struct wlr_input_device *device;
	struct wl_listener key;
	struct wl_list link;
	struct xkb_keymap *keymap;
	struct xkb_state *xkb_state;
	void *data;
};

struct pointer_state {
	struct compositor_state *compositor;
	struct wlr_input_device *device;
	struct wl_listener motion;
	struct wl_listener motion_absolute;
	struct wl_listener button;
	struct wl_listener axis;
	struct wl_list link;
	void *data;
};

struct compositor_state {
	void (*output_add_cb)(struct output_state *s);
	void (*keyboard_add_cb)(struct keyboard_state *s);
	void (*output_frame_cb)(struct output_state *s, struct timespec *ts);
	void (*output_remove_cb)(struct output_state *s);
	void (*keyboard_remove_cb)(struct keyboard_state *s);
	void (*keyboard_key_cb)(struct keyboard_state *s, xkb_keysym_t sym,
			enum wlr_key_state key_state);
	void (*pointer_motion_cb)(struct pointer_state *s,
			double d_x, double d_y);
	void (*pointer_button_cb)(struct pointer_state *s,
			uint32_t button, enum wlr_button_state state);
	void (*pointer_axis_cb)(struct pointer_state *s,
		enum wlr_axis_source source,
		enum wlr_axis_orientation orientation,
		double delta);

	struct wl_display *display;
	struct wl_event_loop *event_loop;
	struct wlr_backend *backend;
	struct wlr_session *session;

	struct wl_list keyboards;
	struct wl_list pointers;
	struct wl_listener input_add;
	struct wl_listener input_remove;

	struct timespec last_frame;
	struct wl_listener output_add;
	struct wl_listener output_remove;
	struct wl_list outputs;

	bool exit;
	void *data;
};

void compositor_init(struct compositor_state *state);
void compositor_run(struct compositor_state *state);

#endif
