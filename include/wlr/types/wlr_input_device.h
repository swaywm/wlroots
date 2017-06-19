#ifndef _WLR_TYPES_INPUT_H
#define _WLR_TYPES_INPUT_H

enum wlr_button_state {
	WLR_BUTTON_RELEASED,
	WLR_BUTTON_PRESSED,
};

enum wlr_input_device_type {
	WLR_INPUT_DEVICE_KEYBOARD,
	WLR_INPUT_DEVICE_POINTER,
	WLR_INPUT_DEVICE_TOUCH,
	WLR_INPUT_DEVICE_TABLET_TOOL,
	WLR_INPUT_DEVICE_TABLET_PAD
};

/* Note: these are circular dependencies */
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_touch.h>
#include <wlr/types/wlr_tablet_tool.h>
#include <wlr/types/wlr_tablet_pad.h>

struct wlr_input_device_state;
struct wlr_input_device_impl;

struct wlr_input_device {
	struct wlr_input_device_state *state;
	struct wlr_input_device_impl *impl;

	enum wlr_input_device_type type;
	int vendor, product;
	char *name;

	/* wlr_input_device.type determines which of these is valid */
	union {
		void *_device;
		struct wlr_keyboard *keyboard;
		struct wlr_pointer *pointer;
		struct wlr_touch *touch;
		struct wlr_tablet_tool *tablet_tool;
		struct wlr_tablet_pad *tablet_pad;
	};
};

#endif
