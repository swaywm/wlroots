#ifndef WLR_INTERFACES_WLR_KEYBOARD_H
#define WLR_INTERFACES_WLR_KEYBOARD_H

#include <wlr/types/wlr_keyboard.h>
#include <stdint.h>

struct wlr_keyboard_impl {
	void (*destroy)(struct wlr_keyboard *keyboard);
	void (*led_update)(struct wlr_keyboard *keyboard, uint32_t leds);
};

void wlr_keyboard_send_key(struct wlr_keyboard *keyboard,
		struct wlr_event_keyboard_key *event);
void wlr_keyboard_send_modifiers(struct wlr_keyboard *keyboard,
		uint32_t mods_depressed, uint32_t mods_latched, uint32_t mods_locked,
		uint32_t group);

#endif
