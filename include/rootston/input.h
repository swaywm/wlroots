#ifndef _ROOTSTON_INPUT_H
#define _ROOTSTON_INPUT_H
#include <xkbcommon/xkbcommon.h>
#include <wayland-server.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/xcursor.h>
#include "rootston/view.h"

struct roots_keyboard {
	struct roots_input *input;
	struct wlr_input_device *device;
	struct wl_listener key;
	struct wl_list link;
	struct xkb_keymap *keymap;
	struct xkb_state *xkb_state;
	xkb_led_index_t leds[WLR_LED_LAST];
	int keymap_fd;
	size_t keymap_size;
};

struct roots_pointer {
	struct roots_input *input;
	struct wlr_input_device *device;
	struct wl_listener motion;
	struct wl_listener motion_absolute;
	struct wl_listener button;
	struct wl_listener axis;
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
	ROOTS_CURSOR_PASSTHROUGH,
	ROOTS_CURSOR_MOVE,
	ROOTS_CURSOR_RESIZE,
	ROOTS_CURSOR_ROTATE,
};

struct roots_input_event {
	uint32_t serial;
	struct wlr_cursor *cursor;
	struct wlr_input_device *device;
};

struct roots_input {
	// TODO: multiseat, multicursor
	struct wlr_cursor *cursor;
	struct wlr_xcursor *xcursor;
	struct wlr_seat *wl_seat;

	enum roots_cursor_mode mode;
	struct roots_view *focused_view;
	struct roots_view *moving_view;
	struct roots_view *resizing_view;
	struct roots_view *rotating_view;
	int offs_x, offs_y;

	// Ring buffer of input events that could trigger move/resize/rotate
	int input_events_idx;
	struct roots_input_event input_events[16];

	struct wl_list keyboards;
	struct wl_list pointers;
	struct wl_list touch;
	struct wl_list tablet_tools;
	struct wl_list tablet_pads;
};

#endif
