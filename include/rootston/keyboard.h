#ifndef _ROOTSTON_KEYBOARD_H
#define _ROOTSTON_KEYBOARD_H

#include <xkbcommon/xkbcommon.h>
#include "rootston/input.h"

#define ROOTS_KEYBOARD_PRESSED_KEYSYMS_CAP 32

struct roots_keyboard {
	struct roots_input *input;
	struct wlr_input_device *device;
	struct wl_listener key;
	struct wl_listener modifiers;
	struct wl_list link;

	xkb_keysym_t pressed_keysyms[ROOTS_KEYBOARD_PRESSED_KEYSYMS_CAP];
};

struct roots_keyboard *roots_keyboard_create(struct wlr_input_device *device,
		struct roots_input *input);
void roots_keyboard_destroy(struct wlr_input_device *device, struct roots_input *input);

void roots_keyboard_handle_key(struct roots_keyboard *keyboard,
		struct wlr_event_keyboard_key *event);

void roots_keyboard_handle_modifiers(struct roots_keyboard *r_keyboard);

#endif
