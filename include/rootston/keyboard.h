#ifndef _ROOTSTON_KEYBOARD_H
#define _ROOTSTON_KEYBOARD_H

#include <xkbcommon/xkbcommon.h>
#include "rootston/input.h"

#define ROOTS_KEYBOARD_PRESSED_KEYSYMS_CAP 32

struct roots_keyboard {
	struct roots_input *input;
	struct roots_seat *seat;
	struct wlr_input_device *device;
	struct keyboard_config *config;
	struct wl_list link;

	struct wl_listener keyboard_key;
	struct wl_listener keyboard_modifiers;

	xkb_keysym_t pressed_keysyms[ROOTS_KEYBOARD_PRESSED_KEYSYMS_CAP];
};

struct roots_keyboard *roots_keyboard_create(struct wlr_input_device *device,
		struct roots_input *input);

void roots_keyboard_destroy(struct roots_keyboard *keyboard);

void roots_keyboard_handle_key(struct roots_keyboard *keyboard,
		struct wlr_event_keyboard_key *event);

void roots_keyboard_handle_modifiers(struct roots_keyboard *r_keyboard,
		struct wlr_event_keyboard_modifiers *event);

#endif
