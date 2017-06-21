#ifndef _WLR_TYPES_TABLET_TOOL_H
#define _WLR_TYPES_TABLET_TOOL_H
#include <wlr/types/wlr_input_device.h>
#include <wayland-server.h>
#include <stdint.h>

struct wlr_tablet_tool_impl;
struct wlr_tablet_tool_state;

struct wlr_tablet_tool {
	struct wlr_tablet_tool_impl *impl;
	struct wlr_tablet_tool_state *state;

	struct {
		struct wl_signal axis;
		struct wl_signal proximity;
		struct wl_signal tip;
		struct wl_signal button;
	} events;
};

enum wlr_tablet_tool_axes {
	WLR_TABLET_TOOL_AXIS_X = 1,
	WLR_TABLET_TOOL_AXIS_Y = 2,
	WLR_TABLET_TOOL_AXIS_DISTANCE = 4,
	WLR_TABLET_TOOL_AXIS_PRESSURE = 8,
	WLR_TABLET_TOOL_AXIS_TILT_X = 16,
	WLR_TABLET_TOOL_AXIS_TILT_Y = 32,
	WLR_TABLET_TOOL_AXIS_ROTATION = 64,
	WLR_TABLET_TOOL_AXIS_SLIDER = 128,
	WLR_TABLET_TOOL_AXIS_WHEEL = 256,
};

struct wlr_event_tablet_tool_axis {
	uint32_t time_sec;
	uint64_t time_usec;
	uint32_t updated_axes;
	double x_mm, y_mm;
	double width_mm, height_mm;
	double pressure;
	double distance;
	double tilt_x, tilt_y;
	double rotation;
	double slider;
	double wheel_delta;
};

enum wlr_tablet_tool_proximity_state {
	WLR_TABLET_TOOL_PROXIMITY_OUT,
	WLR_TABLET_TOOL_PROXIMITY_IN,
};

struct wlr_event_tablet_tool_proximity {
	uint32_t time_sec;
	uint64_t time_usec;
	double x, y;
	double width_mm, height_mm;
	enum wlr_tablet_tool_proximity_state state;
};

enum wlr_tablet_tool_tip_state {
	WLR_TABLET_TOOL_TIP_UP,
	WLR_TABLET_TOOL_TIP_DOWN,
};

struct wlr_event_tablet_tool_tip {
	uint32_t time_sec;
	uint64_t time_usec;
	double x, y;
	double width_mm, height_mm;
	enum wlr_tablet_tool_tip_state state;
};

struct wlr_event_tablet_tool_button {
	uint32_t time_sec;
	uint64_t time_usec;
	uint32_t button;
	enum wlr_button_state state;
};

#endif
