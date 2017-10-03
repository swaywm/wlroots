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
	for (uint32_t i = 0; i < WLR_LED_LAST; ++i) {
		if (xkb_state_led_index_is_active(keyboard->xkb_state, keyboard->leds[i])) {
			leds |= (1 << i);
		}
	}
	wlr_keyboard_led_update(keyboard, leds);
}

void wlr_keyboard_update_state(struct wlr_keyboard *keyboard,
		struct wlr_event_keyboard_key *event) {
	uint32_t keycode = event->keycode + 8;
	xkb_state_update_key(keyboard->xkb_state, keycode,
		event->state == WLR_KEY_PRESSED ?  XKB_KEY_DOWN : XKB_KEY_UP);
	keyboard_led_update(keyboard);
	wl_signal_emit(&keyboard->events.key, event);
}

void wlr_keyboard_init(struct wlr_keyboard *kb,
		struct wlr_keyboard_impl *impl) {
	kb->impl = impl;
	wl_signal_init(&kb->events.key);
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
	const char *led_names[3] = {
		XKB_LED_NAME_NUM,
		XKB_LED_NAME_CAPS,
		XKB_LED_NAME_SCROLL
	};
	for (uint32_t i = 0; i < 3; ++i) {
		kb->leds[i] = xkb_map_led_get_index(kb->keymap, led_names[i]);
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
