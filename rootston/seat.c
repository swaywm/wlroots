#include <wayland-server.h>
#include <stdlib.h>
#include <string.h>

#include <wlr/util/log.h>

#include "rootston/input.h"
#include "rootston/seat.h"
#include "rootston/keyboard.h"

static void handle_keyboard_key(struct wl_listener *listener, void *data) {
	struct roots_keyboard *keyboard =
		wl_container_of(listener, keyboard, keyboard_key);
	struct wlr_event_keyboard_key *event = data;
	roots_keyboard_handle_key(keyboard, event);
}

static void handle_keyboard_modifiers(struct wl_listener *listener,
		void *data) {
	struct roots_keyboard *keyboard =
		wl_container_of(listener, keyboard, keyboard_modifiers);
	struct wlr_event_keyboard_modifiers *event = data;
	roots_keyboard_handle_modifiers(keyboard, event);
}

static void handle_cursor_motion(struct wl_listener *listener, void *data) {
	// TODO
}

static void handle_cursor_motion_absolute(struct wl_listener *listener, void *data) {
	// TODO
}

static void handle_cursor_button(struct wl_listener *listener, void *data) {
	// TODO
}

static void handle_cursor_axis(struct wl_listener *listener, void *data) {
	// TODO
}

static void handle_touch_down(struct wl_listener *listener, void *data) {
	// TODO
}

static void handle_touch_up(struct wl_listener *listener, void *data) {
	// TODO
}

static void handle_touch_motion(struct wl_listener *listener, void *data) {
	// TODO
}

static void handle_tool_axis(struct wl_listener *listener, void *data) {
	// TODO
}

static void handle_tool_tip(struct wl_listener *listener, void *data) {
	// TODO
}

static void seat_reset_device_mappings(struct roots_seat *seat, struct wlr_input_device *device) {
	struct wlr_cursor *cursor = seat->cursor;
	struct roots_config *config = seat->input->config;

	wlr_cursor_map_input_to_output(cursor, device, NULL);
	struct device_config *dconfig;
	if ((dconfig = config_get_device(config, device))) {
		wlr_cursor_map_input_to_region(cursor, device, dconfig->mapped_box);
	}
}

static void seat_set_device_output_mappings(struct roots_seat *seat,
		struct wlr_input_device *device, struct wlr_output *output) {
	struct wlr_cursor *cursor = seat->cursor;
	struct roots_config *config = seat->input->config;
	struct device_config *dconfig;
	dconfig = config_get_device(config, device);
	if (dconfig && dconfig->mapped_output &&
			strcmp(dconfig->mapped_output, output->name) == 0) {
		wlr_cursor_map_input_to_output(cursor, device, output);
	}
}

static void roots_seat_configure_cursor(struct roots_seat *seat) {
	struct roots_config *config = seat->input->config;
	struct roots_desktop *desktop = seat->input->server->desktop;
	struct wlr_cursor *cursor = seat->cursor;

	struct roots_pointer *pointer;
	struct roots_touch *touch;
	struct roots_tablet_tool *tablet_tool;
	struct roots_output *output;

	// reset mappings
	wlr_cursor_map_to_output(cursor, NULL);
	wl_list_for_each(pointer, &seat->pointers, link) {
		seat_reset_device_mappings(seat, pointer->device);
	}
	wl_list_for_each(touch, &seat->touch, link) {
		seat_reset_device_mappings(seat, touch->device);
	}
	wl_list_for_each(tablet_tool, &seat->tablet_tools, link) {
		seat_reset_device_mappings(seat, tablet_tool->device);
	}

	// configure device to output mappings
	const char *mapped_output = config->cursor.mapped_output;
	wl_list_for_each(output, &desktop->outputs, link) {
		if (mapped_output && strcmp(mapped_output, output->wlr_output->name) == 0) {
			wlr_cursor_map_to_output(cursor, output->wlr_output);
		}

		wl_list_for_each(pointer, &seat->pointers, link) {
			seat_set_device_output_mappings(seat, pointer->device, output->wlr_output);
		}
		wl_list_for_each(tablet_tool, &seat->tablet_tools, link) {
			seat_set_device_output_mappings(seat, tablet_tool->device, output->wlr_output);
		}
		wl_list_for_each(touch, &seat->touch, link) {
			seat_set_device_output_mappings(seat, touch->device, output->wlr_output);
		}
	}
}

static void roots_seat_init_cursor(struct roots_seat *seat) {
	struct wlr_cursor *cursor = wlr_cursor_create();
	if (!cursor) {
		return;
	}
	seat->cursor = cursor;

	// add output layout and configure
	struct roots_desktop *desktop = seat->input->server->desktop;
	wlr_cursor_attach_output_layout(cursor, desktop->layout);
	roots_seat_configure_cursor(seat);
	// TODO configure cursor by seat

	// add input signals
	wl_signal_add(&cursor->events.motion, &seat->cursor_motion);
	seat->cursor_motion.notify = handle_cursor_motion;

	wl_signal_add(&cursor->events.motion_absolute,
		&seat->cursor_motion_absolute);
	seat->cursor_motion_absolute.notify = handle_cursor_motion_absolute;

	wl_signal_add(&cursor->events.button, &seat->cursor_button);
	seat->cursor_button.notify = handle_cursor_button;

	wl_signal_add(&cursor->events.axis, &seat->cursor_axis);
	seat->cursor_axis.notify = handle_cursor_axis;

	wl_signal_add(&cursor->events.touch_down, &seat->cursor_touch_down);
	seat->cursor_touch_down.notify = handle_touch_down;

	wl_signal_add(&cursor->events.touch_up, &seat->cursor_touch_up);
	seat->cursor_touch_up.notify = handle_touch_up;

	wl_signal_add(&cursor->events.touch_motion, &seat->cursor_touch_motion);
	seat->cursor_touch_motion.notify = handle_touch_motion;

	wl_signal_add(&cursor->events.tablet_tool_axis, &seat->cursor_tool_axis);
	seat->cursor_tool_axis.notify = handle_tool_axis;

	wl_signal_add(&cursor->events.tablet_tool_tip, &seat->cursor_tool_tip);
	seat->cursor_tool_tip.notify = handle_tool_tip;
}

struct roots_seat *roots_seat_create(struct roots_input *input, char *name) {
	struct roots_seat *seat = calloc(1, sizeof(struct roots_seat));
	if (!seat) {
		return NULL;
	}

	roots_seat_init_cursor(seat);
	if (!seat->cursor) {
		free(seat);
		return NULL;
	}

	seat->seat = wlr_seat_create(input->server->wl_display, name);
	if (!seat->seat) {
		free(seat);
		wlr_cursor_destroy(seat->cursor);
		return NULL;
	}

	wlr_seat_set_capabilities(seat->seat,
		WL_SEAT_CAPABILITY_KEYBOARD |
		WL_SEAT_CAPABILITY_POINTER |
		WL_SEAT_CAPABILITY_TOUCH);

	seat->input = input;

	wl_list_insert(&input->seats, &seat->link);
	wl_list_init(&seat->keyboards);

	return seat;
}

void roots_seat_destroy(struct roots_seat *seat) {
	// TODO
}

static void seat_add_keyboard(struct roots_seat *seat, struct wlr_input_device *device) {
	struct roots_keyboard *keyboard = roots_keyboard_create(device, seat->input);
	keyboard->seat = seat;

	wl_list_insert(&seat->keyboards, &keyboard->seat_link);

	keyboard->keyboard_key.notify = handle_keyboard_key;
	wl_signal_add(&keyboard->device->keyboard->events.key,
		&keyboard->keyboard_key);

	keyboard->keyboard_modifiers.notify = handle_keyboard_modifiers;
	wl_signal_add(&keyboard->device->keyboard->events.modifiers,
		&keyboard->keyboard_modifiers);
}

static void seat_add_pointer(struct roots_seat *seat, struct wlr_input_device *device) {
	struct roots_pointer *pointer = calloc(sizeof(struct roots_pointer), 1);
	if (!pointer) {
		wlr_log(L_ERROR, "could not allocate pointer for seat");
		return;
	}

	device->data = pointer;
	pointer->device = device;
	pointer->seat = seat;
	wl_list_insert(&seat->pointers, &pointer->link);
	wlr_cursor_attach_input_device(seat->cursor, device);
	roots_seat_configure_cursor(seat);
}

static void seat_add_touch(struct roots_seat *seat, struct wlr_input_device *device) {
	struct roots_touch *touch = calloc(sizeof(struct roots_touch), 1);
	if (!touch) {
		wlr_log(L_ERROR, "could not allocate touch for seat");
		return;
	}

	device->data = touch;
	touch->device = device;
	touch->seat = seat;
	wl_list_insert(&seat->touch, &touch->link);
	wlr_cursor_attach_input_device(seat->cursor, device);
	roots_seat_configure_cursor(seat);
}

static void seat_add_tablet_pad(struct roots_seat *seat, struct wlr_input_device *device) {
	// TODO
}

static void seat_add_tablet_tool(struct roots_seat *seat, struct wlr_input_device *device) {
	struct roots_tablet_tool *tablet_tool = calloc(sizeof(struct roots_tablet_tool), 1);
	if (!tablet_tool) {
		wlr_log(L_ERROR, "could not allocate tablet_tool for seat");
		return;
	}

	device->data = tablet_tool;
	tablet_tool->device = device;
	tablet_tool->seat = seat;
	wl_list_insert(&seat->tablet_tools, &tablet_tool->link);
	wlr_cursor_attach_input_device(seat->cursor, device);
	roots_seat_configure_cursor(seat);
}

void roots_seat_add_device(struct roots_seat *seat,
		struct wlr_input_device *device) {
	switch (device->type) {
	case WLR_INPUT_DEVICE_KEYBOARD:
		seat_add_keyboard(seat, device);
		break;
	case WLR_INPUT_DEVICE_POINTER:
		seat_add_pointer(seat, device);
		break;
	case WLR_INPUT_DEVICE_TOUCH:
		seat_add_touch(seat, device);
		break;
	case WLR_INPUT_DEVICE_TABLET_PAD:
		seat_add_tablet_pad(seat, device);
		break;
	case WLR_INPUT_DEVICE_TABLET_TOOL:
		seat_add_tablet_tool(seat, device);
		break;
	}
}

void roots_seat_remove_device(struct roots_seat *seat,
		struct wlr_input_device *device) {
	// TODO
}
