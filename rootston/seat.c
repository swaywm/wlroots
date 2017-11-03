#include <wayland-server.h>
#include <stdlib.h>

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

struct roots_seat *roots_seat_create(struct roots_input *input, char *name) {
	struct roots_seat *seat = calloc(1, sizeof(struct roots_seat));
	if (!seat) {
		return NULL;
	}

	seat->seat = wlr_seat_create(input->server->wl_display, name);
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

void roots_seat_add_keyboard(struct roots_seat *seat,
		struct roots_keyboard *keyboard) {
	if (keyboard->seat) {
		roots_seat_remove_keyboard(keyboard->seat, keyboard);
	}
	keyboard->seat = seat;
	wl_list_insert(&seat->keyboards, &keyboard->seat_link);

	keyboard->keyboard_key.notify = handle_keyboard_key;
	wl_signal_add(&keyboard->device->keyboard->events.key,
		&keyboard->keyboard_key);

	keyboard->keyboard_modifiers.notify = handle_keyboard_modifiers;
	wl_signal_add(&keyboard->device->keyboard->events.modifiers,
		&keyboard->keyboard_modifiers);
}

void roots_seat_remove_keyboard(struct roots_seat *seat,
		struct roots_keyboard *keyboard) {
	// TODO
}

