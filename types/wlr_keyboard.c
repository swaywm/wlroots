#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-server.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/interfaces/wlr_keyboard.h>
#include <wlr/util/log.h>

int os_create_anonymous_file(off_t size);

static void keyboard_led_update(struct wlr_keyboard *keyboard) {
	uint32_t leds = 0;
	for (uint32_t i = 0; i < WLR_LED_COUNT; ++i) {
		if (xkb_state_led_index_is_active(keyboard->xkb_state,
				keyboard->led_indexes[i])) {
			leds |= (1 << i);
		}
	}
	wlr_keyboard_led_update(keyboard, leds);
}

static void keyboard_modifier_update(struct wlr_keyboard *keyboard) {
	xkb_mod_mask_t depressed = xkb_state_serialize_mods(keyboard->xkb_state,
		XKB_STATE_MODS_DEPRESSED);
	xkb_mod_mask_t latched = xkb_state_serialize_mods(keyboard->xkb_state,
		XKB_STATE_MODS_LATCHED);
	xkb_mod_mask_t locked = xkb_state_serialize_mods(keyboard->xkb_state,
		XKB_STATE_MODS_LOCKED);
	xkb_mod_mask_t group = xkb_state_serialize_layout(keyboard->xkb_state,
		XKB_STATE_LAYOUT_EFFECTIVE);
	if (depressed == keyboard->modifiers.depressed &&
			latched == keyboard->modifiers.latched &&
			locked == keyboard->modifiers.locked &&
			group == keyboard->modifiers.group) {
		return;
	}

	keyboard->modifiers.depressed = depressed;
	keyboard->modifiers.latched = latched;
	keyboard->modifiers.locked = locked;
	keyboard->modifiers.group = group;

	wl_signal_emit(&keyboard->events.modifiers, keyboard);
}

void wlr_keyboard_update_state(struct wlr_keyboard *keyboard,
		struct wlr_event_keyboard_key *event) {
	uint32_t keycode = event->keycode + 8;
	xkb_state_update_key(keyboard->xkb_state, keycode,
		event->state == WLR_KEY_PRESSED ? XKB_KEY_DOWN : XKB_KEY_UP);
	keyboard_led_update(keyboard);
	keyboard_modifier_update(keyboard);
	wl_signal_emit(&keyboard->events.key, event);
}

void wlr_keyboard_init(struct wlr_keyboard *kb,
		struct wlr_keyboard_impl *impl) {
	kb->impl = impl;
	wl_signal_init(&kb->events.key);
	wl_signal_init(&kb->events.modifiers);
	wl_signal_init(&kb->events.keymap);
}

void wlr_keyboard_destroy(struct wlr_keyboard *kb) {
	if (kb && kb->impl && kb->impl->destroy) {
		kb->impl->destroy(kb);
	} else {
		wl_list_remove(&kb->events.key.listener_list);
	}
	xkb_state_unref(kb->xkb_state);
	xkb_map_unref(kb->keymap);
	close(kb->keymap_fd);
	free(kb);
}

void wlr_keyboard_led_update(struct wlr_keyboard *kb, uint32_t leds) {
	if (kb->impl && kb->impl->led_update) {
		kb->impl->led_update(kb, leds);
	}
}

void wlr_keyboard_set_keymap(struct wlr_keyboard *kb,
		struct xkb_keymap *keymap) {
	wlr_log(L_DEBUG, "Keymap set");
	kb->keymap = keymap;
	assert(kb->xkb_state = xkb_state_new(kb->keymap));

	const char *led_names[WLR_LED_COUNT] = {
		XKB_LED_NAME_NUM,
		XKB_LED_NAME_CAPS,
		XKB_LED_NAME_SCROLL,
	};
	for (size_t i = 0; i < WLR_LED_COUNT; ++i) {
		kb->led_indexes[i] = xkb_map_led_get_index(kb->keymap, led_names[i]);
	}

	const char *mod_names[WLR_MODIFIER_COUNT] = {
		XKB_MOD_NAME_SHIFT,
		XKB_MOD_NAME_CAPS,
		XKB_MOD_NAME_CTRL, // "Control"
		XKB_MOD_NAME_ALT, // "Mod1"
		XKB_MOD_NAME_NUM, // "Mod2"
		"Mod3",
		XKB_MOD_NAME_LOGO, // "Mod4"
		"Mod5",
	};
	// TODO: there's also "Ctrl", "Alt"?
	for (size_t i = 0; i < WLR_MODIFIER_COUNT; ++i) {
		kb->mod_indexes[i] = xkb_map_mod_get_index(kb->keymap, mod_names[i]);
	}

	char *keymap_str = xkb_keymap_get_as_string(kb->keymap,
		XKB_KEYMAP_FORMAT_TEXT_V1);
	kb->keymap_size = strlen(keymap_str) + 1;
	kb->keymap_fd = os_create_anonymous_file(kb->keymap_size);
	void *ptr = mmap(NULL, kb->keymap_size,
		PROT_READ | PROT_WRITE, MAP_SHARED, kb->keymap_fd, 0);
	strcpy(ptr, keymap_str);
	free(keymap_str);

	wl_signal_emit(&kb->events.keymap, kb);
}

uint32_t wlr_keyboard_get_modifiers(struct wlr_keyboard *kb) {
	xkb_mod_mask_t mask = kb->modifiers.depressed | kb->modifiers.latched;
	uint32_t modifiers = 0;
	for (size_t i = 0; i < WLR_MODIFIER_COUNT; ++i) {
		if (kb->mod_indexes[i] != XKB_MOD_INVALID &&
				(mask & (1 << kb->mod_indexes[i]))) {
			modifiers |= (1 << i);
		}
	}
	return modifiers;
}
