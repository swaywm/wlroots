#include <stdlib.h>
#include <string.h>
#include <wayland-server.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/interfaces/wlr_keyboard.h>

struct wlr_keyboard *wlr_keyboard_create(struct wlr_keyboard_impl *impl,
		struct wlr_keyboard_state *state) {
	struct wlr_keyboard *kb = calloc(1, sizeof(struct wlr_keyboard));
	kb->impl = impl;
	kb->state = state;
	wl_signal_init(&kb->events.key);
	return kb;
}

void wlr_keyboard_destroy(struct wlr_keyboard *kb) {
	if (!kb) return;
	if (kb->impl) {
		kb->impl->destroy(kb->state);
	}
	free(kb);
}

void wlr_keyboard_led_update(struct wlr_keyboard *kb, uint32_t leds) {
	if (kb->impl) {
		kb->impl->led_update(kb->state, leds);
	}
}
