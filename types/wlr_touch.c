#include <stdlib.h>
#include <string.h>
#include <wayland-server.h>
#include <wlr/types/wlr_touch.h>
#include <wlr/interfaces/wlr_touch.h>

struct wlr_touch *wlr_touch_create(struct wlr_touch_impl *impl,
		struct wlr_touch_state *state) {
	struct wlr_touch *touch = calloc(1, sizeof(struct wlr_touch));
	touch->impl = impl;
	touch->state = state;
	wl_signal_init(&touch->events.down);
	wl_signal_init(&touch->events.up);
	wl_signal_init(&touch->events.motion);
	wl_signal_init(&touch->events.cancel);
	return touch;
}

void wlr_touch_destroy(struct wlr_touch *touch) {
	if (!touch) return;
	if (touch->impl) {
		touch->impl->destroy(touch->state);
	}
	free(touch);
}
