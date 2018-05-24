#define _POSIX_C_SOURCE 199309L

#include <wlr/util/log.h>
#include <wlr/types/wlr_virtual_keyboard_v1.h>
#include "rootston/virtual_keyboard.h"
#include "rootston/seat.h"

void handle_virtual_keyboard(struct wl_listener *listener, void *data) {
	struct roots_desktop *desktop =
		wl_container_of(listener, desktop, virtual_keyboard_new);
	struct wlr_virtual_keyboard_v1 *keyboard = data;

	struct roots_seat *seat = input_seat_from_wlr_seat(desktop->server->input,
		keyboard->seat);
	if (!seat) {
		wlr_log(L_ERROR, "could not find roots seat");
		return;
	}

	roots_seat_add_device(seat, &keyboard->input_device);
}
