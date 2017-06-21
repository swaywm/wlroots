#define _XOPEN_SOURCE 500
#include <stdlib.h>
#include <string.h>
#include <wayland-server.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/interfaces/wlr_input_device.h>
#include <wlr/interfaces/wlr_keyboard.h>
#include <wlr/interfaces/wlr_pointer.h>
#include <wlr/interfaces/wlr_touch.h>
#include <wlr/interfaces/wlr_tablet_tool.h>
#include <wlr/interfaces/wlr_tablet_pad.h>
#include <wlr/util/log.h>

struct wlr_input_device *wlr_input_device_create(
		enum wlr_input_device_type type,
		struct wlr_input_device_impl *impl,
		struct wlr_input_device_state *state,
		const char *name, int vendor, int product) {
	struct wlr_input_device *dev = calloc(1, sizeof(struct wlr_input_device));
	dev->type = type;
	dev->impl = impl;
	dev->state = state;
	dev->name = strdup(name);
	dev->vendor = vendor;
	dev->product = product;
	return dev;
}

void wlr_input_device_destroy(struct wlr_input_device *dev) {
	if (!dev) return;
	if (dev->impl && dev->impl->destroy && dev->state) {
		dev->impl->destroy(dev->state);
	}
	if (dev->_device) {
		switch (dev->type) {
		case WLR_INPUT_DEVICE_KEYBOARD:
			wlr_keyboard_destroy(dev->keyboard);
			break;
		case WLR_INPUT_DEVICE_POINTER:
			wlr_pointer_destroy(dev->pointer);
			break;
		case WLR_INPUT_DEVICE_TOUCH:
			wlr_touch_destroy(dev->touch);
			break;
		case WLR_INPUT_DEVICE_TABLET_TOOL:
			wlr_tablet_tool_destroy(dev->tablet_tool);
			break;
		case WLR_INPUT_DEVICE_TABLET_PAD:
			wlr_tablet_pad_destroy(dev->tablet_pad);
			break;
		default:
			wlr_log(L_DEBUG, "Warning: leaking memory %p %p %d",
					dev->_device, dev, dev->type);
			break;
		}
	}
	free(dev->name);
	free(dev);
}
