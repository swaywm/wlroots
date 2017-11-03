#ifndef _ROOTSTON_SEAT_H
#define _ROOTSTON_SEAT_H

#include <wayland-server.h>

#include "rootston/input.h"
#include "rootston/keyboard.h"

struct roots_seat {
	struct roots_input *input;
	struct wlr_seat *seat;
	struct wl_list keyboards;
	struct wl_list link;
};

struct roots_seat *roots_seat_create(struct roots_input *input, char *name);

void roots_seat_destroy(struct roots_seat *seat);

void roots_seat_add_keyboard(struct roots_seat *seat,
		struct roots_keyboard *keyboard);

void roots_seat_remove_keyboard(struct roots_seat *seat,
		struct roots_keyboard *keyboard);

#endif
