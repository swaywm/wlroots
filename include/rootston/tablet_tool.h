#ifndef _ROOTSTON_TABLET_TOOL_H
#define _ROOTSTON_TABLET_TOOL_H

#include <wayland-server.h>
#include "rootston/input.h"

struct roots_tablet_tool {
	struct roots_input *input;
	struct wlr_input_device *device;
	struct wl_listener axis;
	struct wl_listener proximity;
	struct wl_listener tip;
	struct wl_listener button;
	struct wl_list link;
};

void tablet_tool_add(struct wlr_input_device *device, struct roots_input *input);
void tablet_tool_remove(struct wlr_input_device *device, struct roots_input *input);

#endif
