#include <stdlib.h>
#include <string.h>
#include <wayland-server.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/interfaces/wlr_keyboard.h>

void wlr_keyboard_init(struct wlr_keyboard *kb,
		struct wlr_keyboard_impl *impl) {
	kb->impl = impl;
	wl_signal_init(&kb->events.key);
}

void wlr_keyboard_destroy(struct wlr_keyboard *kb) {
	if (!kb) {
		return;
	}
	
	if (kb->impl && kb->impl->destroy) {
		kb->impl->destroy(kb);
	} else {
		free(kb);
	}
}

void wlr_keyboard_led_update(struct wlr_keyboard *kb, uint32_t leds) {
	if (kb->impl && kb->impl->led_update) {
		kb->impl->led_update(kb, leds);
	}
}
