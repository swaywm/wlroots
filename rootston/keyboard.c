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

static void keyboard_led_update(struct roots_keyboard *keyboard) {
	uint32_t leds = 0;
	for (uint32_t i = 0; i < WLR_LED_LAST; ++i) {
		if (xkb_state_led_index_is_active(
					keyboard->xkb_state, keyboard->leds[i])) {
			leds |= (1 << i);
		}
	}
	wlr_keyboard_led_update(keyboard->device->keyboard, leds);
}

static void keyboard_key_notify(struct wl_listener *listener, void *data) {
	struct wlr_event_keyboard_key *event = data;
	struct roots_keyboard *keyboard = wl_container_of(listener, keyboard, key);
	struct roots_input *input = keyboard->input;
	struct roots_server *server = input->server;
	uint32_t keycode = event->keycode + 8;
	enum wlr_key_state key_state = event->state;
	const xkb_keysym_t *syms;
	int nsyms = xkb_state_key_get_syms(keyboard->xkb_state, keycode, &syms);
	xkb_state_update_key(keyboard->xkb_state, keycode,
		event->state == WLR_KEY_PRESSED ?  XKB_KEY_DOWN : XKB_KEY_UP);
	keyboard_led_update(keyboard);
	for (int i = 0; i < nsyms; ++i) {
		xkb_keysym_t sym = syms[i];
		char name[64];
		int l = xkb_keysym_get_name(sym, name, sizeof(name));
		if (l != -1 && l != sizeof(name)) {
			wlr_log(L_DEBUG, "Key event: %s %s", name,
					key_state == WLR_KEY_PRESSED ? "pressed" : "released");
		}
		// TODO: pass key to clients
		if (sym == XKB_KEY_Escape) {
			// TEMPORARY, probably
			wl_display_terminate(server->wl_display);
		} else if (key_state == WLR_KEY_PRESSED &&
				sym >= XKB_KEY_XF86Switch_VT_1 &&
				sym <= XKB_KEY_XF86Switch_VT_12) {
			if (wlr_backend_is_multi(server->backend)) {
				struct wlr_session *session =
					wlr_multi_get_session(server->backend);
				if (session) {
					wlr_session_change_vt(session, sym - XKB_KEY_XF86Switch_VT_1 + 1);
				}
			}
		}
	}
}

void keyboard_add(struct wlr_input_device *device, struct roots_input *input) {
	struct roots_keyboard *keyboard = calloc(sizeof(struct roots_keyboard), 1);
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
	assert(keyboard->keymap = xkb_map_new_from_names(context, &rules,
				XKB_KEYMAP_COMPILE_NO_FLAGS));
	xkb_context_unref(context);
	assert(keyboard->xkb_state = xkb_state_new(keyboard->keymap));
	const char *led_names[3] = {
		XKB_LED_NAME_NUM,
		XKB_LED_NAME_CAPS,
		XKB_LED_NAME_SCROLL
	};
	for (uint32_t i = 0; i < 3; ++i) {
		keyboard->leds[i] = xkb_map_led_get_index(keyboard->keymap, led_names[i]);
	}
}
