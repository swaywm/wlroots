#include <stdlib.h>
#include <wayland-server.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/util/log.h>
#include "rootston/input.h"
#include "rootston/tablet_tool.h"

void tablet_tool_add(struct wlr_input_device *device, struct roots_input *input) {
	struct roots_tablet_tool *tool = calloc(sizeof(struct roots_tablet_tool), 1);
	device->data = tool;
	tool->device = device;
	tool->input = input;
	wl_list_insert(&input->tablet_tools, &tool->link);
	wlr_cursor_attach_input_device(input->cursor, device);
	cursor_load_config(input->server->config, input->cursor,
			input, input->server->desktop);
}

void tablet_tool_remove(struct wlr_input_device *device, struct roots_input *input) {
	struct roots_tablet_tool *tablet_tool = device->data;
	wlr_cursor_detach_input_device(input->cursor, device);
	wl_list_remove(&tablet_tool->link);
	free(tablet_tool);
}
