#ifndef WLR_TYPES_WLR_TOUCH_H
#define WLR_TYPES_WLR_TOUCH_H

#include <wayland-server.h>
#include <stdint.h>

struct wlr_touch_impl;

struct wlr_touch {
	struct wlr_touch_impl *impl;

	struct {
		struct wl_signal down;
		struct wl_signal up;
		struct wl_signal motion;
		struct wl_signal cancel;
	} events;

	void *data;
};

struct wlr_event_touch_down {
	struct wlr_input_device *device;
	uint32_t time_msec;
	int32_t slot;
	double x_mm, y_mm;
	double width_mm, height_mm;
};

struct wlr_event_touch_up {
	struct wlr_input_device *device;
	uint32_t time_msec;
	int32_t slot;
};

struct wlr_event_touch_motion {
	struct wlr_input_device *device;
	uint32_t time_msec;
	int32_t slot;
	double x_mm, y_mm;
	double width_mm, height_mm;
};

struct wlr_event_touch_cancel {
	struct wlr_input_device *device;
	uint32_t time_msec;
	int32_t slot;
};

#endif
