#include <stdlib.h>
#include <string.h>
#include <wayland-server.h>
#include <wlr/types/wlr_tablet_pad.h>
#include <wlr/interfaces/wlr_tablet_pad.h>

struct wlr_tablet_pad *wlr_tablet_pad_create(struct wlr_tablet_pad_impl *impl,
		struct wlr_tablet_pad_state *state) {
	struct wlr_tablet_pad *pad = calloc(1, sizeof(struct wlr_tablet_pad));
	pad->impl = impl;
	pad->state = state;
	wl_signal_init(&pad->events.button);
	wl_signal_init(&pad->events.ring);
	wl_signal_init(&pad->events.strip);
	return pad;
}

void wlr_tablet_pad_destroy(struct wlr_tablet_pad *pad) {
	if (!pad) return;
	if (pad->impl) {
		pad->impl->destroy(pad->state);
	}
	free(pad);
}
