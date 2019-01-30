#ifndef ROOTSTON_SEAT_H
#define ROOTSTON_SEAT_H

#include <wayland-server.h>
#include "rootston/input.h"
#include "rootston/keyboard.h"
#include "rootston/layers.h"
#include "rootston/switch.h"
#include "rootston/text_input.h"

struct roots_seat {
	struct roots_input *input;
	struct wlr_seat *seat;
	struct roots_cursor *cursor;
	struct wl_list link; // roots_input::seats

	// coordinates of the first touch point if it exists
	int32_t touch_id;
	double touch_x, touch_y;

	// If the focused layer is set, views cannot receive keyboard focus
	struct wlr_layer_surface_v1 *focused_layer;

	struct roots_input_method_relay im_relay;

	// If non-null, only this client can receive input events
	struct wl_client *exclusive_client;

	struct wl_list views; // roots_seat_view::link
	bool has_focus;

	struct roots_drag_icon *drag_icon; // can be NULL

	struct wl_list keyboards;
	struct wl_list pointers;
	struct wl_list switches;
	struct wl_list touch;
	struct wl_list tablets;
	struct wl_list tablet_pads;

	struct wl_listener request_set_selection;
	struct wl_listener request_set_primary_selection;
	struct wl_listener new_drag_icon;
	struct wl_listener destroy;
};

struct roots_seat_view {
	struct roots_seat *seat;
	struct roots_view *view;

	bool has_button_grab;
	double grab_sx;
	double grab_sy;

	struct wl_list link; // roots_seat::views

	struct wl_listener view_unmap;
	struct wl_listener view_destroy;
};

struct roots_drag_icon {
	struct roots_seat *seat;
	struct wlr_drag_icon *wlr_drag_icon;

	double x, y;

	struct wl_listener surface_commit;
	struct wl_listener map;
	struct wl_listener unmap;
	struct wl_listener destroy;
};

struct roots_pointer {
	struct roots_seat *seat;
	struct wlr_input_device *device;
	struct wl_listener device_destroy;
	struct wl_list link;
};

struct roots_touch {
	struct roots_seat *seat;
	struct wlr_input_device *device;
	struct wl_listener device_destroy;
	struct wl_list link;
};

struct roots_tablet {
	struct roots_seat *seat;
	struct wlr_input_device *device;
	struct wlr_tablet_v2_tablet *tablet_v2;

	struct wl_listener device_destroy;
	struct wl_listener axis;
	struct wl_listener proximity;
	struct wl_listener tip;
	struct wl_listener button;
	struct wl_list link;
};

struct roots_tablet_pad {
	struct wl_list link;
	struct wlr_tablet_v2_tablet_pad *tablet_v2_pad;

	struct roots_seat *seat;
	struct wlr_input_device *device;

	struct wl_listener device_destroy;
	struct wl_listener attach;
	struct wl_listener button;
	struct wl_listener ring;
	struct wl_listener strip;

	struct roots_tablet *tablet;
	struct wl_listener tablet_destroy;
};

struct roots_tablet_tool {
	struct wl_list link;
	struct wl_list tool_link;
	struct wlr_tablet_v2_tablet_tool *tablet_v2_tool;

	struct roots_seat *seat;
	double tilt_x, tilt_y;

	struct wl_listener set_cursor;
	struct wl_listener tool_destroy;

	struct roots_tablet *current_tablet;
	struct wl_listener tablet_destroy;
};

struct roots_pointer_constraint {
	struct wlr_pointer_constraint_v1 *constraint;

	struct wl_listener destroy;
};

struct roots_seat *roots_seat_create(struct roots_input *input, char *name);

void roots_seat_destroy(struct roots_seat *seat);

void roots_seat_add_device(struct roots_seat *seat,
		struct wlr_input_device *device);

void roots_seat_configure_cursor(struct roots_seat *seat);

void roots_seat_configure_xcursor(struct roots_seat *seat);

bool roots_seat_has_meta_pressed(struct roots_seat *seat);

struct roots_view *roots_seat_get_focus(struct roots_seat *seat);

void roots_seat_set_focus(struct roots_seat *seat, struct roots_view *view);

void roots_seat_set_focus_layer(struct roots_seat *seat,
		struct wlr_layer_surface_v1 *layer);

void roots_seat_cycle_focus(struct roots_seat *seat);

void roots_seat_begin_move(struct roots_seat *seat, struct roots_view *view);

void roots_seat_begin_resize(struct roots_seat *seat, struct roots_view *view,
		uint32_t edges);

void roots_seat_begin_rotate(struct roots_seat *seat, struct roots_view *view);

void roots_seat_end_compositor_grab(struct roots_seat *seat);

struct roots_seat_view *roots_seat_view_from_view( struct roots_seat *seat,
	struct roots_view *view);

void roots_drag_icon_update_position(struct roots_drag_icon *icon);

void roots_drag_icon_damage_whole(struct roots_drag_icon *icon);

void roots_seat_set_exclusive_client(struct roots_seat *seat,
		struct wl_client *client);

bool roots_seat_allow_input(struct roots_seat *seat,
		struct wl_resource *resource);

#endif
