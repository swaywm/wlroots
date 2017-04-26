#include <stdlib.h>
#include <wayland-client.h>
#include "wlr/wayland.h"
#include "wlr/common/list.h"

void wlr_wl_seat_free(struct wlr_wl_seat *seat) {
	if (!seat) return;
	if (seat->wl_seat) wl_seat_destroy(seat->wl_seat);
	if (seat->name) free(seat->name);
	if (seat->keyboards) {
		// TODO: free children
		list_free(seat->keyboards);
	}
	if (seat->pointers) {
		// TODO: free children
		list_free(seat->keyboards);
	}
	free(seat);
}
