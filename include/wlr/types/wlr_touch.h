#ifndef _WLR_TYPES_TOUCH_H
#define _WLR_TYPES_TOUCH_H
#include <wayland-server.h>
#include <stdint.h>

struct wlr_touch_state;
struct wlr_touch_impl;

struct wlr_touch {
	struct wlr_touch_state *state;
	struct wlr_touch_impl *impl;

	struct {
		struct wl_signal down;
		struct wl_signal up;
		struct wl_signal motion;
		struct wl_signal cancel;
	} events;
};

struct wlr_event_touch_down {
	uint32_t time_sec;
	uint64_t time_usec;
	int32_t slot;
	double x_mm, y_mm;
	double width_mm, height_mm;
};

struct wlr_event_touch_up {
	uint32_t time_sec;
	uint64_t time_usec;
	int32_t slot;
};

struct wlr_event_touch_motion {
	uint32_t time_sec;
	uint64_t time_usec;
	int32_t slot;
	double x_mm, y_mm;
	double width_mm, height_mm;
};

struct wlr_event_touch_cancel {
	uint32_t time_sec;
	uint64_t time_usec;
	int32_t slot;
};

#endif
