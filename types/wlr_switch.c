#include <stdlib.h>
#include <string.h>
#include <wayland-server.h>
#include <wlr/interfaces/wlr_switch.h>
#include <wlr/types/wlr_switch.h>

void wlr_switch_init(struct wlr_switch *lid_switch,
		struct wlr_switch_impl *impl) {
	lid_switch->impl = impl;
	wl_signal_init(&lid_switch->events.toggle);
}

void wlr_switch_destroy(struct wlr_switch *lid_switch) {
	if (!lid_switch) {
		return;
	}
	if (lid_switch->impl && lid_switch->impl->destroy) {
		lid_switch->impl->destroy(lid_switch);
	} else {
		free(lid_switch);
	}
}
