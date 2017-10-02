#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <wayland-server.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/backend/multi.h>
#include <wlr/backend/session.h>
#include <wlr/util/log.h>
#include <xkbcommon/xkbcommon.h>
#include "rootston/input.h"

static ssize_t keyboard_pressed_key_index(struct roots_keyboard *keyboard,
		uint32_t keycode) {
	for (size_t i = 0; i < ROOTS_KEYBOARD_PRESSED_KEYS_CAP; i++) {
		if (keyboard->pressed_keys[i].keycode == keycode) {
			return i;
		}
	}
	return -1;
}

static void keyboard_key_press(struct roots_keyboard *keyboard,
		struct roots_keyboard_key *key) {
	struct roots_server *server = keyboard->input->server;

	ssize_t i = keyboard_pressed_key_index(keyboard, key->keycode);
	if (i < 0) {
		i = keyboard_pressed_key_index(keyboard, 0);
		if (i >= 0) {
			memcpy(&keyboard->pressed_keys[i], key,
				sizeof(struct roots_keyboard_key));
		}
	}

	for (size_t i = 0; i < key->syms_len; i++) {
		xkb_keysym_t sym = key->syms[i];
		if (sym == XKB_KEY_Escape) {
			// TEMPORARY, probably
			wl_display_terminate(server->wl_display);
		} else if (sym >= XKB_KEY_XF86Switch_VT_1 &&
				sym <= XKB_KEY_XF86Switch_VT_12) {
			if (wlr_backend_is_multi(server->backend)) {
				struct wlr_session *session =
					wlr_multi_get_session(server->backend);
				if (session) {
					unsigned vt = sym - XKB_KEY_XF86Switch_VT_1 + 1;
					wlr_session_change_vt(session, vt);
				}
			}
		}
	}
}

static void keyboard_key_release(struct roots_keyboard *keyboard,
		struct roots_keyboard_key *key) {
	ssize_t i = keyboard_pressed_key_index(keyboard, key->keycode);
	if (i >= 0) {
		struct roots_keyboard_key *pressed_key = &keyboard->pressed_keys[i];
		memset(pressed_key, 0, sizeof(struct roots_keyboard_key));
	}
}

static void keyboard_key_notify(struct wl_listener *listener, void *data) {
	struct wlr_event_keyboard_key *event = data;
	struct roots_keyboard *keyboard = wl_container_of(listener, keyboard, key);

	uint32_t keycode = event->keycode + 8;
	const xkb_keysym_t *syms;
	int syms_len = xkb_state_key_get_syms(keyboard->device->keyboard->xkb_state,
		keycode, &syms);

	struct roots_keyboard_key *key = calloc(1,
		sizeof(struct roots_keyboard_key));
	if (key == NULL) {
		return;
	}
	key->keycode = keycode;
	key->syms = syms;
	key->syms_len = syms_len;

	switch (event->state) {
	case WLR_KEY_PRESSED:
		keyboard_key_press(keyboard, key);
		break;
	case WLR_KEY_RELEASED:
		keyboard_key_release(keyboard, key);
		break;
	}

	free(key);
}

void keyboard_add(struct wlr_input_device *device, struct roots_input *input) {
	struct roots_keyboard *keyboard = calloc(sizeof(struct roots_keyboard), 1);
	device->data = keyboard;
	keyboard->device = device;
	keyboard->input = input;
	wl_list_init(&keyboard->key.link);
	keyboard->key.notify = keyboard_key_notify;
	wl_signal_add(&device->keyboard->events.key, &keyboard->key);
	wl_list_insert(&input->keyboards, &keyboard->link);

	struct xkb_rule_names rules;
	memset(&rules, 0, sizeof(rules));
	rules.rules = getenv("XKB_DEFAULT_RULES");
	rules.model = getenv("XKB_DEFAULT_MODEL");
	rules.layout = getenv("XKB_DEFAULT_LAYOUT");
	rules.variant = getenv("XKB_DEFAULT_VARIANT");
	rules.options = getenv("XKB_DEFAULT_OPTIONS");
	struct xkb_context *context;
	assert(context = xkb_context_new(XKB_CONTEXT_NO_FLAGS));
	wlr_keyboard_set_keymap(device->keyboard, xkb_map_new_from_names(context,
				&rules, XKB_KEYMAP_COMPILE_NO_FLAGS));
	xkb_context_unref(context);
	wlr_seat_attach_keyboard(input->wl_seat, device);
}

void keyboard_remove(struct wlr_input_device *device, struct roots_input *input) {
	struct roots_keyboard *keyboard = device->data;
	wlr_seat_detach_keyboard(input->wl_seat, device->keyboard);
	wl_list_remove(&keyboard->key.link);
	wl_list_remove(&keyboard->link);
	free(keyboard);
}
