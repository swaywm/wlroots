#ifndef _ROOTSTON_INPUT_H
#define _ROOTSTON_INPUT_H
#include <xkbcommon/xkbcommon.h>
#include <wayland-server.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/xcursor.h>
#include "rootston/config.h"
#include "rootston/view.h"
#include "rootston/server.h"

#define ROOTS_KEYBOARD_PRESSED_KEYSYMS_CAP 32

struct roots_keyboard {
	struct roots_input *input;
	struct wlr_input_device *device;
	struct wl_listener key;
	struct wl_list link;

	xkb_keysym_t pressed_keysyms[ROOTS_KEYBOARD_PRESSED_KEYSYMS_CAP];
};

struct roots_pointer {
	struct roots_input *input;
	struct wlr_input_device *device;
	struct wl_list link;
};

struct roots_touch {
	struct roots_input *input;
	struct wlr_input_device *device;
	struct wl_list link;
};

// TODO: tablet pad
struct roots_tablet_tool {
	struct roots_input *input;
	struct wlr_input_device *device;
	struct wl_listener axis;
	struct wl_listener proximity;
	struct wl_listener tip;
	struct wl_listener button;
	struct wl_list link;
};

enum roots_cursor_mode {
	ROOTS_CURSOR_PASSTHROUGH = 0,
	ROOTS_CURSOR_MOVE = 1,
	ROOTS_CURSOR_RESIZE = 2,
	ROOTS_CURSOR_ROTATE = 3,
};

enum roots_cursor_resize_edge {
	ROOTS_CURSOR_RESIZE_EDGE_TOP = 1,
	ROOTS_CURSOR_RESIZE_EDGE_BOTTOM = 2,
	ROOTS_CURSOR_RESIZE_EDGE_LEFT = 4,
	ROOTS_CURSOR_RESIZE_EDGE_RIGHT = 8,
};

struct roots_input_event {
	uint32_t serial;
	struct wlr_cursor *cursor;
	struct wlr_input_device *device;
};

struct roots_drag_icon {
	struct wlr_surface *surface;
	struct wl_list link; // roots_input::drag_icons

	int32_t sx;
	int32_t sy;

	struct wl_listener surface_destroy;
	struct wl_listener surface_commit;
};

struct roots_touch_point {
	struct roots_touch *device;
	int32_t slot;
	double x, y;
	struct wl_list link;
};

struct roots_input {
	struct roots_config *config;
	struct roots_server *server;

	// TODO: multiseat, multicursor
	struct wlr_cursor *cursor;
	struct wlr_xcursor_theme *xcursor_theme;
	struct wlr_seat *wl_seat;
	struct wl_list drag_icons;
	struct wl_client *cursor_client;

	enum roots_cursor_mode mode;
	struct roots_view *active_view, *last_active_view;
	int offs_x, offs_y;
	int view_x, view_y, view_width, view_height;
	float view_rotation;
	uint32_t resize_edges;

	// Ring buffer of input events that could trigger move/resize/rotate
	int input_events_idx;
	struct roots_input_event input_events[16];

	struct wl_list keyboards;
	struct wl_list pointers;
	struct wl_list touch;
	struct wl_list tablet_tools;

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

void pointer_add(struct wlr_input_device *device, struct roots_input *input);
void pointer_remove(struct wlr_input_device *device, struct roots_input *input);
void keyboard_add(struct wlr_input_device *device, struct roots_input *input);
void keyboard_remove(struct wlr_input_device *device, struct roots_input *input);
void touch_add(struct wlr_input_device *device, struct roots_input *input);
void touch_remove(struct wlr_input_device *device, struct roots_input *input);
void tablet_tool_add(struct wlr_input_device *device, struct roots_input *input);
void tablet_tool_remove(struct wlr_input_device *device, struct roots_input *input);

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

#endif
