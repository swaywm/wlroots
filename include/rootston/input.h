#ifndef ROOTSTON_INPUT_H
#define ROOTSTON_INPUT_H

#include <wayland-server.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_seat.h>
#include "rootston/config.h"
#include "rootston/cursor.h"
#include "rootston/server.h"
#include "rootston/view.h"

struct roots_input {
	struct roots_config *config;
	struct roots_server *server;

	struct wl_listener new_input;

	struct wl_list seats; // roots_seat::link
};

struct roots_input *input_create(struct roots_server *server,
		struct roots_config *config);
void input_destroy(struct roots_input *input);

struct roots_seat *input_seat_from_wlr_seat(struct roots_input *input,
		struct wlr_seat *seat);

bool input_view_has_focus(struct roots_input *input, struct roots_view *view);

struct roots_seat *input_get_seat(struct roots_input *input, char *name);

struct roots_seat *input_last_active_seat(struct roots_input *input);

void input_update_cursor_focus(struct roots_input *input);

#endif
