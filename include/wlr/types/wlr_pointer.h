#ifndef _WLR_TYPES_POINTER_H
#define _WLR_TYPES_POINTER_H
#include <wlr/types/wlr_input_device.h>
#include <wayland-server.h>
#include <stdint.h>

struct wlr_pointer_state;
struct wlr_pointer_impl;

struct wlr_pointer {
	struct wlr_pointer_state *state;
	struct wlr_pointer_impl *impl;

	struct {
		struct wl_signal motion;
		struct wl_signal motion_absolute;
		struct wl_signal button;
		struct wl_signal axis;
	} events;
};

struct wlr_event_pointer_motion {
	uint32_t time_sec;
	uint64_t time_usec;
	double delta_x, delta_y;
};

struct wlr_event_pointer_motion_absolute {
	uint32_t time_sec;
	uint64_t time_usec;
	double x_mm, y_mm;
	double width_mm, height_mm;
};

struct wlr_event_pointer_button {
	uint32_t time_sec;
	uint64_t time_usec;
	uint32_t button;
	enum wlr_button_state state;
};

enum wlr_axis_source {
	WLR_AXIS_SOURCE_WHEEL,
	WLR_AXIS_SOURCE_FINGER,
	WLR_AXIS_SOURCE_CONTINUOUS,
	WLR_AXIS_SOURCE_WHEEL_TILT,
};

enum wlr_axis_orientation {
	WLR_AXIS_ORIENTATION_VERTICAL,
	WLR_AXIS_ORIENTATION_HORIZONTAL,
};

struct wlr_event_pointer_axis {
	uint32_t time_sec;
	uint64_t time_usec;
	enum wlr_axis_source source;
	enum wlr_axis_orientation orientation;
	double delta;
};

#endif
