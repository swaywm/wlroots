#ifndef _WLR_TYPES_KEYBOARD_H
#define _WLR_TYPES_KEYBOARD_H
#include <wayland-server.h>
#include <stdint.h>

enum WLR_KEYBOARD_LED {
	WLR_LED_NUM_LOCK = 1,
	WLR_LED_CAPS_LOCK = 2,
	WLR_LED_SCROLL_LOCK = 4,
	WLR_LED_LAST
};

struct wlr_keyboard_state;
struct wlr_keyboard_impl;

struct wlr_keyboard {
	struct wlr_keyboard_state *state;
	struct wlr_keyboard_impl *impl;

	struct {
		struct wl_signal key;
	} events;
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

#endif
