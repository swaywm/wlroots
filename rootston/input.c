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

	char *seat_name = ROOTS_CONFIG_DEFAULT_SEAT_NAME;
	struct roots_device_config *dc =
		roots_config_get_device(input->config, device);
	if (dc) {
		seat_name = dc->seat;
	}

	struct roots_seat *seat = input_get_seat(input, seat_name);
	if (!seat) {
		wlr_log(L_ERROR, "could not create roots seat");
		return;
	}

	wlr_log(L_DEBUG, "New input device: %s (%d:%d) %s seat:%s", device->name,
			device->vendor, device->product, device_type(device->type), seat_name);

	roots_seat_add_device(seat, device);
}

static void input_remove_notify(struct wl_listener *listener, void *data) {
	struct wlr_input_device *device = data;
	struct roots_input *input = wl_container_of(listener, input, input_remove);

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

	wl_list_init(&input->seats);

	input->input_add.notify = input_add_notify;
	wl_signal_add(&server->backend->events.input_add, &input->input_add);
	input->input_remove.notify = input_remove_notify;
	wl_signal_add(&server->backend->events.input_remove, &input->input_remove);

	return input;
}

void input_destroy(struct roots_input *input) {
	// TODO
}

struct roots_seat *input_seat_from_wlr_seat(struct roots_input *input,
		struct wlr_seat *wlr_seat) {
	struct roots_seat *seat = NULL;
	wl_list_for_each(seat, &input->seats, link) {
		if (seat->seat == wlr_seat) {
			return seat;
		}
	}
	return seat;
}

bool input_view_has_focus(struct roots_input *input, struct roots_view *view) {
	if (!view) {
		return false;
	}
	struct roots_seat *seat;
	wl_list_for_each(seat, &input->seats, link) {
		if (seat->focus == view) {
			return true;
		}
	}

	return false;
}
