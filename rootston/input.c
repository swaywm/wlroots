#define _POSIX_C_SOURCE 200112L
#include <assert.h>
#include <stdlib.h>
#include <time.h>
#include <wayland-server.h>
#include <wlr/backend/libinput.h>
#include <wlr/config.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/util/log.h>
#include <wlr/xcursor.h>
#if WLR_HAS_XWAYLAND
#include <wlr/xwayland.h>
#endif
#include "rootston/config.h"
#include "rootston/input.h"
#include "rootston/keyboard.h"
#include "rootston/seat.h"
#include "rootston/server.h"

static const char *device_type(enum wlr_input_device_type type) {
	switch (type) {
	case WLR_INPUT_DEVICE_KEYBOARD:
		return "keyboard";
	case WLR_INPUT_DEVICE_POINTER:
		return "pointer";
	case WLR_INPUT_DEVICE_SWITCH:
		return "switch";
	case WLR_INPUT_DEVICE_TOUCH:
		return "touch";
	case WLR_INPUT_DEVICE_TABLET_TOOL:
		return "tablet tool";
	case WLR_INPUT_DEVICE_TABLET_PAD:
		return "tablet pad";
	}
	return NULL;
}

struct roots_seat *input_get_seat(struct roots_input *input, char *name) {
	struct roots_seat *seat = NULL;
	wl_list_for_each(seat, &input->seats, link) {
		if (strcmp(seat->seat->name, name) == 0) {
			return seat;
		}
	}

	seat = roots_seat_create(input, name);
	return seat;
}

static void handle_new_input(struct wl_listener *listener, void *data) {
	struct wlr_input_device *device = data;
	struct roots_input *input = wl_container_of(listener, input, new_input);

	char *seat_name = ROOTS_CONFIG_DEFAULT_SEAT_NAME;
	struct roots_device_config *dc =
		roots_config_get_device(input->config, device);
	if (dc) {
		seat_name = dc->seat;
	}

	struct roots_seat *seat = input_get_seat(input, seat_name);
	if (!seat) {
		wlr_log(WLR_ERROR, "could not create roots seat");
		return;
	}

	wlr_log(WLR_DEBUG, "New input device: %s (%d:%d) %s seat:%s", device->name,
			device->vendor, device->product, device_type(device->type), seat_name);

	roots_seat_add_device(seat, device);

	if (dc && wlr_input_device_is_libinput(device)) {
		struct libinput_device *libinput_dev =
			wlr_libinput_get_device_handle(device);

		wlr_log(WLR_DEBUG, "input has config, tap_enabled: %d\n", dc->tap_enabled);
		if (dc->tap_enabled) {
			libinput_device_config_tap_set_enabled(libinput_dev,
					LIBINPUT_CONFIG_TAP_ENABLED);
		}
	}
}

struct roots_input *input_create(struct roots_server *server,
		struct roots_config *config) {
	wlr_log(WLR_DEBUG, "Initializing roots input");
	assert(server->desktop);

	struct roots_input *input = calloc(1, sizeof(struct roots_input));
	if (input == NULL) {
		return NULL;
	}

	input->config = config;
	input->server = server;

	wl_list_init(&input->seats);

	input->new_input.notify = handle_new_input;
	wl_signal_add(&server->backend->events.new_input, &input->new_input);

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
		if (view == roots_seat_get_focus(seat)) {
			return true;
		}
	}

	return false;
}

static inline int64_t timespec_to_msec(const struct timespec *a) {
	return (int64_t)a->tv_sec * 1000 + a->tv_nsec / 1000000;
}

void input_update_cursor_focus(struct roots_input *input) {
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);

	struct roots_seat *seat;
	wl_list_for_each(seat, &input->seats, link) {
		roots_cursor_update_position(seat->cursor, timespec_to_msec(&now));
	}
}
