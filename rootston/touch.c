#include <stdlib.h>
#include <wayland-server.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_pointer.h>
#include "rootston/input.h"
#include "rootston/touch.h"

// TODO: we'll likely want touch events to both control the cursor *and* be
// submitted directly to the seat.

void touch_add(struct wlr_input_device *device, struct roots_input *input) {
	struct roots_touch *touch = calloc(sizeof(struct roots_touch), 1);
	device->data = touch;
	touch->device = device;
	touch->input = input;
	wl_list_insert(&input->touch, &touch->link);
	wlr_cursor_attach_input_device(input->cursor, device);
	cursor_load_config(input->server->config, input->cursor,
			input, input->server->desktop);
}

void touch_remove(struct wlr_input_device *device, struct roots_input *input) {
	struct roots_touch *touch = device->data;
	wlr_cursor_detach_input_device(input->cursor, device);
	wl_list_remove(&touch->link);
	free(touch);
}
