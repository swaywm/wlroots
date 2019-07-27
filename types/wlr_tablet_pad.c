#include <stdlib.h>
#include <string.h>
#include <wayland-server-core.h>
#include <wlr/interfaces/wlr_tablet_pad.h>
#include <wlr/types/wlr_tablet_pad.h>

void wlr_tablet_pad_init(struct wlr_tablet_pad *pad,
		struct wlr_tablet_pad_impl *impl) {
	pad->impl = impl;
	wl_signal_init(&pad->events.button);
	wl_signal_init(&pad->events.ring);
	wl_signal_init(&pad->events.strip);
	wl_signal_init(&pad->events.attach_tablet);
}

void wlr_tablet_pad_destroy(struct wlr_tablet_pad *pad) {
	if (!pad) {
		return;
	}

	wlr_list_for_each(&pad->paths, free);
	wlr_list_finish(&pad->paths);

	if (pad->impl && pad->impl->destroy) {
		pad->impl->destroy(pad);
	} else {
		free(pad);
	}
}
