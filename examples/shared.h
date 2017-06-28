#ifndef _EXAMPLE_SHARED_H
#define _EXAMPLE_SHARED_H
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200112L
#endif
#include <time.h>
#include <stdbool.h>
#include <xkbcommon/xkbcommon.h>
#include <wayland-server-protocol.h>
#include <wlr/backend.h>
#include <wlr/session.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_input_device.h>

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
	xkb_led_index_t leds[WLR_LED_LAST];
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

struct touch_state {
	struct compositor_state *compositor;
	struct wlr_input_device *device;
	struct wl_listener down;
	struct wl_listener up;
	struct wl_listener motion;
	struct wl_listener cancel;
	struct wl_list link;
	void *data;
};

struct tablet_tool_state {
	struct compositor_state *compositor;
	struct wlr_input_device *device;
	struct wl_listener axis;
	struct wl_listener proximity;
	struct wl_listener tip;
	struct wl_listener button;
	struct wl_list link;
	void *data;
};

struct tablet_pad_state {
	struct compositor_state *compositor;
	struct wlr_input_device *device;
	struct wl_listener button;
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
	void (*pointer_motion_absolute_cb)(struct pointer_state *s,
			double x, double y);
	void (*pointer_button_cb)(struct pointer_state *s,
			uint32_t button, enum wlr_button_state state);
	void (*pointer_axis_cb)(struct pointer_state *s,
		enum wlr_axis_source source,
		enum wlr_axis_orientation orientation,
		double delta);
	void (*touch_down_cb)(struct touch_state *s, int32_t slot,
		double x, double y, double width, double height);
	void (*touch_motion_cb)(struct touch_state *s, int32_t slot,
		double x, double y, double width, double height);
	void (*touch_up_cb)(struct touch_state *s, int32_t slot);
	void (*touch_cancel_cb)(struct touch_state *s, int32_t slot);
	void (*tool_axis_cb)(struct tablet_tool_state *s,
			struct wlr_event_tablet_tool_axis *event);
	void (*tool_proximity_cb)(struct tablet_tool_state *s,
			enum wlr_tablet_tool_proximity_state proximity);
	void (*tool_tip_cb)(struct tablet_tool_state *s,
			enum wlr_tablet_tool_tip_state state);
	void (*tool_button_cb)(struct tablet_tool_state *s,
			uint32_t button, enum wlr_button_state state);
	void (*pad_button_cb)(struct tablet_pad_state *s,
			uint32_t button, enum wlr_button_state state);

	struct wl_display *display;
	struct wl_event_loop *event_loop;
	struct wlr_backend *backend;
	struct wlr_session *session;

	struct wl_list keyboards;
	struct wl_list pointers;
	struct wl_list touch;
	struct wl_list tablet_tools;
	struct wl_list tablet_pads;
	struct wl_listener input_add;
	struct wl_listener input_remove;

	struct timespec last_frame;
	struct wl_listener output_add;
	struct wl_listener output_remove;
	struct wl_list outputs;

	void *data;
};

void compositor_init(struct compositor_state *state);
void compositor_run(struct compositor_state *state);

#endif
