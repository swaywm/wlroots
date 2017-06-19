#ifndef _WLR_INTERFACES_INPUT_DEVICE_H
#define _WLR_INTERFACES_INPUT_DEVICE_H
#include <wlr/types/wlr_input_device.h>

struct wlr_input_device_impl {
	void (*destroy)(struct wlr_input_device_state *state);
};

struct wlr_input_device *wlr_input_device_create(
		enum wlr_input_device_type type,
		struct wlr_input_device_impl *impl,
		struct wlr_input_device_state *state,
		const char *name, int vendor, int product);
void wlr_input_device_destroy(struct wlr_input_device *dev);

#endif
