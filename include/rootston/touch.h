#ifndef _ROOTSTON_TOUCH_H
#define _ROOTSTON_TOUCH_H

#include <wayland-server.h>

struct roots_touch {
	struct roots_input *input;
	struct wlr_input_device *device;
	struct wl_list link;
};

struct roots_touch_point {
	struct roots_touch *device;
	int32_t slot;
	double x, y;
	struct wl_list link;
};

void touch_add(struct wlr_input_device *device, struct roots_input *input);
void touch_remove(struct wlr_input_device *device, struct roots_input *input);

#endif
