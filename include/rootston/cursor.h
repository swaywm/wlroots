#ifndef ROOTSTON_CURSOR_H
#define ROOTSTON_CURSOR_H

#include <wlr/types/wlr_pointer_constraints_v1.h>
#include "rootston/seat.h"

enum roots_cursor_mode {
	ROOTS_CURSOR_PASSTHROUGH = 0,
	ROOTS_CURSOR_MOVE = 1,
	ROOTS_CURSOR_RESIZE = 2,
	ROOTS_CURSOR_ROTATE = 3,
};

struct roots_cursor {
	struct roots_seat *seat;
	struct wlr_cursor *cursor;

	struct wlr_pointer_constraint_v1 *active_constraint;
	pixman_region32_t confine; // invalid if active_constraint == NULL

	const char *default_xcursor;

	enum roots_cursor_mode mode;

	// state from input (review if this is necessary)
	struct wlr_xcursor_manager *xcursor_manager;
	struct wlr_seat *wl_seat;
	struct wl_client *cursor_client;
	int offs_x, offs_y;
	int view_x, view_y, view_width, view_height;
	float view_rotation;
	uint32_t resize_edges;

	struct roots_seat_view *pointer_view;
	struct wlr_surface *wlr_surface;

	struct wl_listener motion;
	struct wl_listener motion_absolute;
	struct wl_listener button;
	struct wl_listener axis;
	struct wl_listener frame;

	struct wl_listener touch_down;
	struct wl_listener touch_up;
	struct wl_listener touch_motion;

	struct wl_listener tool_axis;
	struct wl_listener tool_tip;
	struct wl_listener tool_proximity;
	struct wl_listener tool_button;

	struct wl_listener request_set_cursor;

	struct wl_listener focus_change;

	struct wl_listener constraint_commit;
};

struct roots_cursor *roots_cursor_create(struct roots_seat *seat);

void roots_cursor_destroy(struct roots_cursor *cursor);

void roots_cursor_handle_motion(struct roots_cursor *cursor,
	struct wlr_event_pointer_motion *event);

void roots_cursor_handle_motion_absolute(struct roots_cursor *cursor,
	struct wlr_event_pointer_motion_absolute *event);

void roots_cursor_handle_button(struct roots_cursor *cursor,
	struct wlr_event_pointer_button *event);

void roots_cursor_handle_axis(struct roots_cursor *cursor,
	struct wlr_event_pointer_axis *event);

void roots_cursor_handle_frame(struct roots_cursor *cursor);

void roots_cursor_handle_touch_down(struct roots_cursor *cursor,
	struct wlr_event_touch_down *event);

void roots_cursor_handle_touch_up(struct roots_cursor *cursor,
	struct wlr_event_touch_up *event);

void roots_cursor_handle_touch_motion(struct roots_cursor *cursor,
	struct wlr_event_touch_motion *event);

void roots_cursor_handle_tool_axis(struct roots_cursor *cursor,
	struct wlr_event_tablet_tool_axis *event);

void roots_cursor_handle_tool_tip(struct roots_cursor *cursor,
	struct wlr_event_tablet_tool_tip *event);

void roots_cursor_handle_request_set_cursor(struct roots_cursor *cursor,
	struct wlr_seat_pointer_request_set_cursor_event *event);

void roots_cursor_handle_focus_change(struct roots_cursor *cursor,
	struct wlr_seat_pointer_focus_change_event *event);

void roots_cursor_handle_constraint_commit(struct roots_cursor *cursor);

void roots_cursor_update_position(struct roots_cursor *cursor,
	uint32_t time);

void roots_cursor_update_focus(struct roots_cursor *cursor);

void roots_cursor_constrain(struct roots_cursor *cursor,
	struct wlr_pointer_constraint_v1 *constraint, double sx, double sy);

#endif
