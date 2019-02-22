#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <string.h>
#include <wayland-server.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/interfaces/wlr_pointer.h>
#include <wlr/interfaces/wlr_input_device.h>
#include <wlr/util/log.h>
#include "backend/rdp.h"
#include "util/signal.h"

static struct wlr_input_device_impl input_device_impl = { 0 };

struct wlr_rdp_input_device *wlr_rdp_pointer_create(
		struct wlr_rdp_backend *backend, struct wlr_rdp_peer_context *context) {
	struct wlr_rdp_input_device *device =
		calloc(1, sizeof(struct wlr_rdp_input_device));
	if (!device) {
		wlr_log(WLR_ERROR, "Failed to allocate RDP input device");
		return NULL;
	}

	int vendor = 0;
	int product = 0;
	const char *name = "rdp";
	struct wlr_input_device *wlr_device = &device->wlr_input_device;
	wlr_input_device_init(wlr_device, WLR_INPUT_DEVICE_POINTER,
			&input_device_impl, name, vendor, product);

	if (!(wlr_device->pointer = calloc(1, sizeof(struct wlr_pointer)))) {
		wlr_log(WLR_ERROR, "Failed to allocate RDP pointer device");
		return NULL;
	}
	wlr_device->output_name = strdup(context->output->wlr_output.name);
	wlr_pointer_init(wlr_device->pointer, NULL);

	wlr_signal_emit_safe(&backend->backend.events.new_input, wlr_device);
	return device;
}
