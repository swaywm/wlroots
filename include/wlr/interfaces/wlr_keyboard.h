#ifndef _WLR_INTERFACE_KEYBOARD_H
#define _WLR_INTERFACE_KEYBOARD_H
#include <wlr/types/wlr_keyboard.h>
#include <stdint.h>

struct wlr_keyboard_impl {
	void (*destroy)(struct wlr_keyboard_state *state);
	void (*led_update)(struct wlr_keyboard_state *state, uint32_t leds);
};

struct wlr_keyboard *wlr_keyboard_create(struct wlr_keyboard_impl *impl,
		struct wlr_keyboard_state *state);
void wlr_keyboard_destroy(struct wlr_keyboard *keyboard);

#endif
