#include <assert.h>
#include <stdlib.h>
#include <wayland-server.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/util/log.h>
#include <wlr/xcursor.h>
#include <wlr/xwayland.h>
#include "rootston/server.h"
#include "rootston/config.h"
#include "rootston/input.h"
#include "rootston/keyboard.h"
#include "rootston/seat.h"

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

static struct roots_seat *input_get_seat(struct roots_input *input, char *name) {
	struct roots_seat *seat = NULL;
	wl_list_for_each(seat, &input->seats, link) {
		if (strcmp(seat->seat->name, name) == 0) {
			return seat;
		}
	}

	seat = roots_seat_create(input, name);
	return seat;
}

static void input_add_notify(struct wl_listener *listener, void *data) {
	struct wlr_input_device *device = data;
	struct roots_input *input = wl_container_of(listener, input, input_add);

	char *seat_name = "seat0";
	struct device_config *dc = config_get_device(input->config, device);
	if (dc) {
		seat_name = dc->seat;
	}

	struct roots_seat *seat = input_get_seat(input, seat_name);
	if (!seat) {
		wlr_log(L_ERROR, "could not create roots seat");
		return;
	}

	wlr_log(L_DEBUG, "New input device: %s (%d:%d) %s", device->name,
			device->vendor, device->product, device_type(device->type));

	roots_seat_add_device(seat, device);
}

static void input_remove_notify(struct wl_listener *listener, void *data) {
	struct wlr_input_device *device = data;
	struct roots_input *input = wl_container_of(listener, input, input_add);

	struct roots_seat *seat;
	wl_list_for_each(seat, &input->seats, link) {
		roots_seat_remove_device(seat, device);
	}
}

struct roots_input *input_create(struct roots_server *server,
		struct roots_config *config) {
	wlr_log(L_DEBUG, "Initializing roots input");
	assert(server->desktop);

	struct roots_input *input = calloc(1, sizeof(struct roots_input));
	if (input == NULL) {
		return NULL;
	}

	input->config = config;
	input->server = server;

	input->xcursor_theme = wlr_xcursor_theme_load("default", 16);
	if (input->xcursor_theme == NULL) {
		wlr_log(L_ERROR, "Cannot load xcursor theme");
		free(input);
		return NULL;
	}

	struct wlr_xcursor *xcursor = get_default_xcursor(input->xcursor_theme);
	if (xcursor == NULL) {
		wlr_log(L_ERROR, "Cannot load xcursor from theme");
		wlr_xcursor_theme_destroy(input->xcursor_theme);
		free(input);
		return NULL;
	}

	if (server->desktop->xwayland != NULL) {
		struct wlr_xcursor_image *xcursor_image = xcursor->images[0];
		wlr_xwayland_set_cursor(server->desktop->xwayland,
			xcursor_image->buffer, xcursor_image->width, xcursor_image->width,
			xcursor_image->height, xcursor_image->hotspot_x,
			xcursor_image->hotspot_y);
	}

	input->wl_seat = wlr_seat_create(server->wl_display, "seat0");
	if (input->wl_seat == NULL) {
		wlr_log(L_ERROR, "Cannot create seat");
		wlr_xcursor_theme_destroy(input->xcursor_theme);
		free(input);
		return NULL;
	}
	wlr_seat_set_capabilities(input->wl_seat, WL_SEAT_CAPABILITY_KEYBOARD
		| WL_SEAT_CAPABILITY_POINTER | WL_SEAT_CAPABILITY_TOUCH);

	wl_list_init(&input->keyboards);
	wl_list_init(&input->pointers);
	wl_list_init(&input->touch);
	wl_list_init(&input->tablet_tools);
	wl_list_init(&input->seats);

	input->input_add.notify = input_add_notify;
	wl_signal_add(&server->backend->events.input_add, &input->input_add);
	input->input_remove.notify = input_remove_notify;
	wl_signal_add(&server->backend->events.input_remove, &input->input_remove);

	input->cursor = wlr_cursor_create();
	cursor_initialize(input);

	struct wlr_xcursor_image *image = xcursor->images[0];
	wlr_cursor_set_image(input->cursor, image->buffer, image->width,
		image->width, image->height, image->hotspot_x, image->hotspot_y);

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
