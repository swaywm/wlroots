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

struct roots_keyboard {
	struct roots_input *input;
	struct wlr_input_device *device;
	struct wl_listener key;
	struct wl_list link;
};

struct roots_pointer {
	struct roots_input *input;
	struct wlr_input_device *device;
	// We don't listen to any pointer events directly - they go through
	// wlr_cursor
	struct wl_list link;
};

struct roots_touch {
	struct roots_input *input;
	struct wlr_input_device *device;
	struct wl_listener down;
	struct wl_listener up;
	struct wl_listener motion;
	struct wl_listener cancel;
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

struct roots_input {
	struct roots_config *config;
	struct roots_server *server;

	// TODO: multiseat, multicursor
	struct wlr_cursor *cursor;
	struct wlr_xcursor *xcursor;
	struct wlr_seat *wl_seat;

	enum roots_cursor_mode mode;
	struct roots_view *active_view;
	int offs_x, offs_y;
	int view_x, view_y, view_width, view_height;
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
	struct wl_listener cursor_tool_axis;
	struct wl_listener cursor_tool_tip;
};

struct roots_input *input_create(struct roots_server *server,
		struct roots_config *config);
void input_destroy(struct roots_input *input);

void pointer_add(struct wlr_input_device *device, struct roots_input *input);
void pointer_remove(struct wlr_input_device *device, struct roots_input *input);
void keyboard_add(struct wlr_input_device *device, struct roots_input *input);
void keyboard_remove(struct wlr_input_device *device, struct roots_input *input);
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

#endif
