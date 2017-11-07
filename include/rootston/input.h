#ifndef _ROOTSTON_INPUT_H
#define _ROOTSTON_INPUT_H
#include <wayland-server.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/xcursor.h>
#include "rootston/cursor.h"
#include "rootston/config.h"
#include "rootston/view.h"
#include "rootston/server.h"

struct roots_input {
	struct roots_config *config;
	struct roots_server *server;

	struct wl_list keyboards;
	struct wl_list pointers;
	struct wl_list touch;
	struct wl_list tablet_tools;
	struct wl_list seats;

	struct wl_listener input_add;
	struct wl_listener input_remove;

	struct wl_listener cursor_motion;
	struct wl_listener cursor_motion_absolute;
	struct wl_listener cursor_button;
	struct wl_listener cursor_axis;

	struct wl_listener cursor_touch_down;
	struct wl_listener cursor_touch_up;
	struct wl_listener cursor_touch_motion;

	struct wl_listener cursor_tool_axis;
	struct wl_listener cursor_tool_tip;

	struct wl_listener pointer_grab_begin;
	struct wl_list touch_points;

	struct wl_listener pointer_grab_end;

	struct wl_listener request_set_cursor;
};

struct roots_input *input_create(struct roots_server *server,
		struct roots_config *config);
void input_destroy(struct roots_input *input);

void cursor_initialize(struct roots_input *input);
void cursor_load_config(struct roots_config *config,
		struct wlr_cursor *cursor,
		struct roots_input *input,
		struct roots_desktop *desktop);
const struct roots_input_event *get_input_event(struct roots_input *input,
		uint32_t serial);
void view_begin_move(struct roots_input *input, struct wlr_cursor *cursor,
		struct roots_view *view);
void view_begin_resize(struct roots_input *input, struct wlr_cursor *cursor,
		struct roots_view *view, uint32_t edges);

struct wlr_xcursor *get_default_xcursor(struct wlr_xcursor_theme *theme);
struct wlr_xcursor *get_move_xcursor(struct wlr_xcursor_theme *theme);
struct wlr_xcursor *get_resize_xcursor(struct wlr_xcursor_theme *theme,
	uint32_t edges);
struct wlr_xcursor *get_rotate_xcursor(struct wlr_xcursor_theme *theme);

void set_view_focus(struct roots_input *input, struct roots_desktop *desktop,
	struct roots_view *view);

struct roots_seat *input_seat_from_wlr_seat(struct roots_input *input,
		struct wlr_seat *seat);

#endif
