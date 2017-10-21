#include <assert.h>
#include <stdlib.h>
#include <wayland-server.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/util/log.h>
#include <wlr/xcursor.h>
#include "rootston/server.h"
#include "rootston/config.h"
#include "rootston/input.h"

static const char *device_type(enum wlr_input_device_type type) {
	switch (type) {
	case WLR_INPUT_DEVICE_KEYBOARD:
		return "keyboard";
	case WLR_INPUT_DEVICE_POINTER:
		return "pointer";
	case WLR_INPUT_DEVICE_TOUCH:
		return "touch";
	case WLR_INPUT_DEVICE_TABLET_TOOL:
		return "tablet tool";
	case WLR_INPUT_DEVICE_TABLET_PAD:
		return "tablet pad";
	}
	return NULL;
}

static void input_add_notify(struct wl_listener *listener, void *data) {
	struct wlr_input_device *device = data;
	struct roots_input *input = wl_container_of(listener, input, input_add);
	wlr_log(L_DEBUG, "New input device: %s (%d:%d) %s", device->name,
			device->vendor, device->product, device_type(device->type));
	switch (device->type) {
	case WLR_INPUT_DEVICE_KEYBOARD:
		keyboard_add(device, input);
		break;
	case WLR_INPUT_DEVICE_POINTER:
		pointer_add(device, input);
		break;
	case WLR_INPUT_DEVICE_TOUCH:
		touch_add(device, input);
		break;
	case WLR_INPUT_DEVICE_TABLET_TOOL:
		tablet_tool_add(device, input);
		break;
	default:
		break;
	}
}

static void input_remove_notify(struct wl_listener *listener, void *data) {
	struct wlr_input_device *device = data;
	struct roots_input *input = wl_container_of(listener, input, input_remove);
	switch (device->type) {
	case WLR_INPUT_DEVICE_KEYBOARD:
		keyboard_remove(device, input);
		break;
	case WLR_INPUT_DEVICE_POINTER:
		pointer_remove(device, input);
		break;
	case WLR_INPUT_DEVICE_TOUCH:
		touch_remove(device, input);
		break;
	case WLR_INPUT_DEVICE_TABLET_TOOL:
		tablet_tool_remove(device, input);
		break;
	default:
		break;
	}
}

struct roots_input *input_create(struct roots_server *server,
		struct roots_config *config) {
	wlr_log(L_DEBUG, "Initializing roots input");
	assert(server->desktop);

	struct roots_input *input = calloc(1, sizeof(struct roots_input));
	assert(input);

	input->config = config;
	input->server = server;

	assert(input->theme = wlr_xcursor_theme_load("default", 16));
	assert(input->xcursor = wlr_xcursor_theme_get_cursor(input->theme, "left_ptr"));

	assert(input->wl_seat = wlr_seat_create(server->wl_display, "seat0"));
	wlr_seat_set_capabilities(input->wl_seat, WL_SEAT_CAPABILITY_KEYBOARD
		| WL_SEAT_CAPABILITY_POINTER | WL_SEAT_CAPABILITY_TOUCH);

	wl_list_init(&input->keyboards);
	wl_list_init(&input->pointers);
	wl_list_init(&input->touch);
	wl_list_init(&input->tablet_tools);

	wl_list_init(&input->input_add.link);
	input->input_add.notify = input_add_notify;
	wl_list_init(&input->input_remove.link);
	input->input_remove.notify = input_remove_notify;

	wl_signal_add(&server->backend->events.input_add,
			&input->input_add);
	wl_signal_add(&server->backend->events.input_remove,
			&input->input_remove);

	input->cursor = wlr_cursor_create();
	cursor_initialize(input);
	wlr_cursor_set_xcursor(input->cursor, input->xcursor);

	wlr_cursor_attach_output_layout(input->cursor, server->desktop->layout);
	wlr_cursor_map_to_region(input->cursor, config->cursor.mapped_box);
	cursor_load_config(config, input->cursor,
		input, server->desktop);

	wl_list_init(&input->drag_icons);

	return input;
}

void input_destroy(struct roots_input *input) {
	// TODO
}
