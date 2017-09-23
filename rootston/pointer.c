#include <stdlib.h>
#include <wayland-server.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/util/log.h>
#include "rootston/input.h"

static void pointer_motion_notify(struct wl_listener *listener, void *data) {
	//struct wlr_event_pointer_motion *event = data;
	struct roots_pointer *pointer = wl_container_of(listener, pointer, motion);
}

static void pointer_motion_absolute_notify(struct wl_listener *listener, void *data) {
	//struct wlr_event_pointer_motion *event = data;
	struct roots_pointer *pointer = wl_container_of(listener, pointer, motion_absolute);
}

void pointer_add(struct wlr_input_device *device, struct roots_input *input) {
	struct roots_pointer *pointer = calloc(sizeof(struct roots_pointer), 1);
	pointer->device = device;
	pointer->input = input;
	wl_list_init(&pointer->motion.link);
	wl_list_init(&pointer->motion_absolute.link);
	wl_list_init(&pointer->button.link);
	wl_list_init(&pointer->axis.link);
	pointer->motion.notify = pointer_motion_notify;
	pointer->motion_absolute.notify = pointer_motion_absolute_notify;
	//pointer->button.notify = pointer_button_notify;
	//pointer->axis.notify = pointer_axis_notify;
	wl_signal_add(&device->pointer->events.motion, &pointer->motion);
	wl_signal_add(&device->pointer->events.motion_absolute, &pointer->motion_absolute);
	wl_signal_add(&device->pointer->events.button, &pointer->button);
	wl_signal_add(&device->pointer->events.axis, &pointer->axis);
	wl_list_insert(&input->pointers, &pointer->link);

	wlr_cursor_attach_input_device(input->cursor, device);
	// TODO: rootston/cursor.c
	//example_config_configure_cursor(sample->config, sample->cursor,
	//	sample->compositor);
}
