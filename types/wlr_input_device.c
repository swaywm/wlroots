#define _XOPEN_SOURCE 500
#include <stdlib.h>
#include <string.h>
#include <wayland-server.h>
#include <wlr/types.h>
#include <wlr/common/list.h>
#include "types.h"

struct wlr_input_device *wlr_input_device_create(
		enum wlr_input_device_type type, const char *name,
		int vendor, int product) {
	struct wlr_input_device *dev = calloc(1, sizeof(struct wlr_input_device));
	dev->type = type;
	dev->name = strdup(name);
	dev->vendor = vendor;
	dev->product = product;
	return dev;
}

void wlr_input_device_destroy(struct wlr_input_device *dev) {
	if (!dev) return;
	free(dev->name);
	free(dev);
}
