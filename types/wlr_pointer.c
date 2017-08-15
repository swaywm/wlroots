#include <stdlib.h>
#include <string.h>
#include <wayland-server.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/interfaces/wlr_pointer.h>

void wlr_pointer_init(struct wlr_pointer *pointer,
		struct wlr_pointer_impl *impl) {
	pointer->impl = impl;
	wl_signal_init(&pointer->events.motion);
	wl_signal_init(&pointer->events.motion_absolute);
	wl_signal_init(&pointer->events.button);
	wl_signal_init(&pointer->events.axis);
}

void wlr_pointer_destroy(struct wlr_pointer *pointer) {
	if (pointer && pointer->impl && pointer->impl->destroy) {
		pointer->impl->destroy(pointer);
	} else {
		free(pointer);
	}
}
