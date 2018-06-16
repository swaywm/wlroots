#include <stdlib.h>
#include <string.h>
#include <wayland-server.h>
#include <wlr/interfaces/wlr_tablet_tool.h>
#include <wlr/types/wlr_tablet_tool.h>

void wlr_tablet_init(struct wlr_tablet *tablet,
		struct wlr_tablet_impl *impl) {
	tablet->impl = impl;
	wl_signal_init(&tablet->events.axis);
	wl_signal_init(&tablet->events.proximity);
	wl_signal_init(&tablet->events.tip);
	wl_signal_init(&tablet->events.button);
}

void wlr_tablet_destroy(struct wlr_tablet *tablet) {
	if (!tablet) {
		return;
	}

	wlr_list_for_each(&tablet->paths, free);
	wlr_list_finish(&tablet->paths);

	if (tablet->impl && tablet->impl->destroy) {
		tablet->impl->destroy(tablet);
	} else {
		free(tablet);
	}
}
