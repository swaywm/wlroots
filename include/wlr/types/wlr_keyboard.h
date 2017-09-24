#ifndef _WLR_TYPES_KEYBOARD_H
#define _WLR_TYPES_KEYBOARD_H
#include <stdint.h>
#include <wayland-server.h>
#include <xkbcommon/xkbcommon.h>

enum WLR_KEYBOARD_LED {
	WLR_LED_NUM_LOCK = 1,
	WLR_LED_CAPS_LOCK = 2,
	WLR_LED_SCROLL_LOCK = 4,
	WLR_LED_LAST
};

struct wlr_keyboard_impl;

struct wlr_keyboard {
	struct wlr_keyboard_impl *impl;
	// TODO: Should this store key repeat info too?

	int keymap_fd;
	size_t keymap_size;
	struct xkb_keymap *keymap;
	struct xkb_state *xkb_state;
	xkb_led_index_t leds[WLR_LED_LAST];

	struct {
		struct wl_signal key;
		struct wl_signal keymap;
	} events;

	void *data;
};

void wlr_keyboard_led_update(struct wlr_keyboard *keyboard, uint32_t leds);

enum wlr_key_state {
	WLR_KEY_RELEASED,
	WLR_KEY_PRESSED,
};

struct wlr_event_keyboard_key {
	uint32_t time_sec;
	uint64_t time_usec;
	uint32_t keycode;
	enum wlr_key_state state;
};

void wlr_keyboard_set_keymap(struct wlr_keyboard *kb,
		struct xkb_keymap *keymap);

#endif
