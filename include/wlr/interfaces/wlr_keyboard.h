#ifndef WLR_INTERFACE_WLR_KEYBOARD_H
#define WLR_INTERFACE_WLR_KEYBOARD_H

#include <wlr/types/wlr_keyboard.h>
#include <stdint.h>

struct wlr_keyboard_impl {
	void (*destroy)(struct wlr_keyboard *keyboard);
	void (*led_update)(struct wlr_keyboard *keyboard, uint32_t leds);
};

void wlr_keyboard_init(struct wlr_keyboard *keyboard, struct wlr_keyboard_impl *impl);
void wlr_keyboard_destroy(struct wlr_keyboard *keyboard);

#endif
