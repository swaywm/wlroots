#ifndef WLR_TYPES_TABLET_TOOL_H
#define WLR_TYPES_TABLET_TOOL_H

#include <stdint.h>
#include <wayland-server.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_list.h>

/*
 * Copy+Paste from libinput, but this should neither use libinput, nor
 * tablet-unstable-v2 headers, so we can't include them
 */
enum wlr_tablet_tool_type {
	WLR_TABLET_TOOL_TYPE_PEN = 1,	/**< A generic pen */
	WLR_TABLET_TOOL_TYPE_ERASER,	/**< Eraser */
	WLR_TABLET_TOOL_TYPE_BRUSH,	/**< A paintbrush-like tool */
	WLR_TABLET_TOOL_TYPE_PENCIL,	/**< Physical drawing tool, e.g.
				             Wacom Inking Pen */
	WLR_TABLET_TOOL_TYPE_AIRBRUSH,	/**< An airbrush-like tool */
	WLR_TABLET_TOOL_TYPE_MOUSE,	/**< A mouse bound to the tablet */
	WLR_TABLET_TOOL_TYPE_LENS,	/**< A mouse tool with a lens */
};

struct wlr_tablet_tool_tool {
	enum wlr_tablet_tool_type type;
	uint64_t hardware_serial;
	uint64_t hardware_wacom;

	// Capabilities
	bool tilt;
	bool pressure;
	bool distance;
	bool rotation;
	bool slider;
	bool wheel;

	struct {
		struct wl_signal destroy;
	} events;
	
	void *data;
};

struct wlr_tablet_tool_impl;

struct wlr_tablet_tool {
	struct wlr_tablet_tool_impl *impl;

	struct {
		struct wl_signal axis;
		struct wl_signal proximity;
		struct wl_signal tip;
		struct wl_signal button;
	} events;

	const char *name;
	struct wlr_list paths; // char *

	void *data;
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
	struct wlr_input_device *device;
	struct wlr_tablet_tool_tool *tool;

	uint32_t time_msec;
	uint32_t updated_axes;
	// From 0..1
	double x, y;
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
	struct wlr_input_device *device;
	struct wlr_tablet_tool_tool *tool;
	uint32_t time_msec;
	// From 0..1
	double x, y;
	enum wlr_tablet_tool_proximity_state state;
};

enum wlr_tablet_tool_tip_state {
	WLR_TABLET_TOOL_TIP_UP,
	WLR_TABLET_TOOL_TIP_DOWN,
};

struct wlr_event_tablet_tool_tip {
	struct wlr_input_device *device;
	struct wlr_tablet_tool_tool *tool;
	uint32_t time_msec;
	// From 0..1
	double x, y;
	enum wlr_tablet_tool_tip_state state;
};

struct wlr_event_tablet_tool_button {
	struct wlr_input_device *device;
	struct wlr_tablet_tool_tool *tool;
	uint32_t time_msec;
	uint32_t button;
	enum wlr_button_state state;
};

#endif
