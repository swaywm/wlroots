#ifndef _ROOTSTON_POINTER_H
#define _ROOTSTON_POINTER_H

#include <wayland-server.h>
#include <wlr/types/wlr_input_device.h>

struct roots_pointer {
	struct roots_input *input;
	struct wlr_input_device *device;
	struct wl_list link;
};

void pointer_add(struct wlr_input_device *device, struct roots_input *input);
void pointer_remove(struct wlr_input_device *device, struct roots_input *input);

#endif
