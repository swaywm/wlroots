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

static ssize_t keyboard_pressed_keysym_index(struct roots_keyboard *keyboard,
		xkb_keysym_t keysym) {
	for (size_t i = 0; i < ROOTS_KEYBOARD_PRESSED_KEYSYMS_CAP; i++) {
		if (keyboard->pressed_keysyms[i] == keysym) {
			return i;
		}
	}
	return -1;
}

static void keyboard_binding_execute(struct roots_keyboard *keyboard,
		char *command) {
	struct roots_server *server = keyboard->input->server;
	if (strcmp(command, "exit") == 0) {
		wl_display_terminate(server->wl_display);
	}
}

static void keyboard_keysym_press(struct roots_keyboard *keyboard,
		xkb_keysym_t keysym) {
	ssize_t i = keyboard_pressed_keysym_index(keyboard, keysym);
	if (i < 0) {
		i = keyboard_pressed_keysym_index(keyboard, XKB_KEY_NoSymbol);
		if (i >= 0) {
			keyboard->pressed_keysyms[i] = keysym;
		}
	}

	if (keysym >= XKB_KEY_XF86Switch_VT_1 &&
			keysym <= XKB_KEY_XF86Switch_VT_12) {
		struct roots_server *server = keyboard->input->server;
		if (wlr_backend_is_multi(server->backend)) {
			struct wlr_session *session =
				wlr_multi_get_session(server->backend);
			if (session) {
				unsigned vt = keysym - XKB_KEY_XF86Switch_VT_1 + 1;
				wlr_session_change_vt(session, vt);
			}
		}
		return;
	}

	struct wl_list *bindings = &keyboard->input->server->config->bindings;
	struct binding_config *bc;
	wl_list_for_each(bc, bindings, link) {
		bool ok = true;
		for (size_t i = 0; i < bc->keysyms_len; i++) {
			ssize_t j = keyboard_pressed_keysym_index(keyboard, bc->keysyms[i]);
			if (j < 0) {
				ok = false;
				break;
			}
		}

		if (ok) {
			keyboard_binding_execute(keyboard, bc->command);
		}
	}
}

static void keyboard_keysym_release(struct roots_keyboard *keyboard,
		xkb_keysym_t keysym) {
	ssize_t i = keyboard_pressed_keysym_index(keyboard, keysym);
	if (i >= 0) {
		keyboard->pressed_keysyms[i] = XKB_KEY_NoSymbol;
	}
}

static void keyboard_key_notify(struct wl_listener *listener, void *data) {
	struct wlr_event_keyboard_key *event = data;
	struct roots_keyboard *keyboard = wl_container_of(listener, keyboard, key);

	uint32_t keycode = event->keycode + 8;
	const xkb_keysym_t *syms;
	int syms_len = xkb_state_key_get_syms(keyboard->device->keyboard->xkb_state,
		keycode, &syms);
	for (int i = 0; i < syms_len; i++) {
		if (event->state == WLR_KEY_PRESSED) {
			keyboard_keysym_press(keyboard, syms[i]);
		} else { // WLR_KEY_RELEASED
			keyboard_keysym_release(keyboard, syms[i]);
		}
	}
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
