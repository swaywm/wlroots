#include <stdlib.h>
#include <wayland-server.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_pointer.h>
#include "rootston/input.h"
#include "rootston/pointer.h"

void pointer_add(struct wlr_input_device *device, struct roots_input *input) {
	struct roots_pointer *pointer = calloc(sizeof(struct roots_pointer), 1);
	device->data = pointer;
	pointer->device = device;
	pointer->input = input;
	wl_list_insert(&input->pointers, &pointer->link);
	wlr_cursor_attach_input_device(input->cursor, device);
	cursor_load_config(input->server->config, input->cursor,
			input, input->server->desktop);
}

void pointer_remove(struct wlr_input_device *device, struct roots_input *input) {
	struct roots_pointer *pointer = device->data;
	wlr_cursor_detach_input_device(input->cursor, device);
	wl_list_remove(&pointer->link);
	free(pointer);
}
