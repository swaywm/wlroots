#ifndef ROOTSTON_SWITCH_H
#define ROOTSTON_SWITCH_H

#include "rootston/input.h"

struct roots_switch {
	struct roots_seat *seat;
	struct wlr_input_device *device;
	struct wl_listener device_destroy;

	struct wl_listener toggle;
	struct wl_list link;
};

void roots_switch_handle_toggle(struct roots_switch *lid_switch,
		struct wlr_event_switch_toggle *event);

#endif
