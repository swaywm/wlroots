#ifndef _ROOTSTON_SEAT_H
#define _ROOTSTON_SEAT_H
#include <wayland-server.h>
#include "rootston/input.h"
#include "rootston/keyboard.h"

struct roots_drag_icon {
	struct wlr_surface *surface;
	struct wl_list link; // roots_seat::drag_icons
	bool mapped;

	int32_t sx;
	int32_t sy;

	struct wl_listener surface_destroy;
	struct wl_listener surface_commit;
};

struct roots_seat {
	struct roots_input *input;
	struct wlr_seat *seat;
	struct roots_cursor *cursor;
	struct wl_list link;
	struct wl_list drag_icons;

	struct roots_view *focus;

	struct wl_list keyboards;
	struct wl_list pointers;
	struct wl_list touch;
	struct wl_list tablet_tools;
};

struct roots_pointer {
	struct roots_seat *seat;
	struct wlr_input_device *device;
	struct wl_list link;
};

struct roots_touch {
	struct roots_seat *seat;
	struct wlr_input_device *device;
	struct wl_list link;
};

struct roots_tablet_tool {
	struct roots_seat *seat;
	struct wlr_input_device *device;
	struct wl_listener axis;
	struct wl_listener proximity;
	struct wl_listener tip;
	struct wl_listener button;
	struct wl_list link;
};

struct roots_seat *roots_seat_create(struct roots_input *input, char *name);

void roots_seat_destroy(struct roots_seat *seat);

void roots_seat_add_device(struct roots_seat *seat,
		struct wlr_input_device *device);

void roots_seat_remove_device(struct roots_seat *seat,
		struct wlr_input_device *device);

void roots_seat_configure_cursor(struct roots_seat *seat);

void roots_seat_configure_xcursor(struct roots_seat *seat);

bool roots_seat_has_meta_pressed(struct roots_seat *seat);

void roots_seat_focus_view(struct roots_seat *seat, struct roots_view *view);

void roots_seat_begin_move(struct roots_seat *seat, struct roots_view *view);

void roots_seat_begin_resize(struct roots_seat *seat, struct roots_view *view,
		uint32_t edges);

void roots_seat_begin_rotate(struct roots_seat *seat, struct roots_view *view);

#endif
