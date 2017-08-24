#include <wlr/types/wlr_cursor.h>
#include <stdlib.h>
#include <assert.h>
#include <wlr/util/log.h>
#include <wayland-server.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_input_device.h>

struct wlr_cursor_device {
	struct wlr_cursor *cursor;
	struct wlr_input_device *device;
	struct wl_list link;

	struct wl_listener motion;
	struct wl_listener motion_absolute;
	struct wl_listener button;
	struct wl_listener axis;
};

struct wlr_cursor_state {
	struct wl_list devices;
	struct wlr_output_layout *layout;
	struct wlr_xcursor *xcursor;
};

struct wlr_cursor *wlr_cursor_init() {
	struct wlr_cursor *cur = calloc(1, sizeof(struct wlr_cursor));
	if (!cur) {
		wlr_log(L_ERROR, "Failed to allocate wlr_cursor");
		return NULL;
	}

	cur->state = calloc(1, sizeof(struct wlr_cursor_state));
	if (!cur->state) {
		wlr_log(L_ERROR, "Failed to allocate wlr_cursor_state");
		return NULL;
	}

	wl_list_init(&cur->state->devices);

	wl_signal_init(&cur->events.motion);
	wl_signal_init(&cur->events.motion_absolute);
	wl_signal_init(&cur->events.button);
	wl_signal_init(&cur->events.axis);

	cur->x = 100;
	cur->y = 100;

	return cur;
}

void wlr_cursor_destroy(struct wlr_cursor *cur) {
	struct wlr_cursor_device *device, *tmp = NULL;
	wl_list_for_each_safe(device, tmp, &cur->state->devices, link) {
		wl_list_remove(&device->link);
		free(device);
	}

	free(cur);
}

void wlr_cursor_set_xcursor(struct wlr_cursor *cur, struct wlr_xcursor *xcur) {
	cur->state->xcursor = xcur;
}

bool wlr_cursor_warp(struct wlr_cursor *cur, double x, double y) {
	assert(cur->state->layout);
	if (!wlr_output_layout_output_at(cur->state->layout, x, y)) {
		return false;
	}

	int hotspot_x = 0;
	int hotspot_y = 0;

	if (cur->state->xcursor && cur->state->xcursor->image_count > 0) {
		struct wlr_xcursor_image *image = cur->state->xcursor->images[0];
		hotspot_x = image->hotspot_x;
		hotspot_y = image->hotspot_y;
	}


	struct wlr_output_layout_output *l_output;
	wl_list_for_each(l_output, &cur->state->layout->outputs, link) {
		int output_x = x;
		int output_y = y;

		// TODO fix double to int rounding issues
		wlr_output_layout_output_coords(cur->state->layout,
			l_output->output, &output_x, &output_y);
		wlr_output_move_cursor(l_output->output, output_x - hotspot_x,
			output_y - hotspot_y);
	}

	return true;
}

void wlr_cursor_move(struct wlr_cursor *cur, double delta_x, double delta_y) {
	assert(cur->state->layout);

	int x = cur->x + delta_x;
	int y = cur->y + delta_y;

	if (!wlr_output_layout_output_at(cur->state->layout, x, y)) {
		int closest_x, closest_y;
		wlr_output_layout_closest_boundary(cur->state->layout, x, y, &closest_x,
			&closest_y);
		x = closest_x;
		y = closest_y;
	}

	if (wlr_cursor_warp(cur, x, y)) {
		cur->x = x;
		cur->y = y;
	}
}

static void handle_pointer_motion(struct wl_listener *listener, void *data) {
	struct wlr_event_pointer_motion *event = data;
	struct wlr_cursor_device *device = wl_container_of(listener, device, motion);
	wl_signal_emit(&device->cursor->events.motion, event);
}

static void handle_pointer_motion_absolute(struct wl_listener *listener, void *data) {
	struct wlr_event_pointer_motion_absolute *event = data;
	struct wlr_cursor_device *device = wl_container_of(listener, device, motion_absolute);
	wl_signal_emit(&device->cursor->events.motion_absolute, event);
}

static void handle_pointer_button(struct wl_listener *listener, void *data) {
	struct wlr_event_pointer_button *event = data;
	struct wlr_cursor_device *device = wl_container_of(listener, device, button);
	wl_signal_emit(&device->cursor->events.button, event);
}

static void handle_pointer_axis(struct wl_listener *listener, void *data) {
	struct wlr_event_pointer_axis *event = data;
	struct wlr_cursor_device *device = wl_container_of(listener, device, axis);
	wl_signal_emit(&device->cursor->events.axis, event);
}

void wlr_cursor_attach_input_device(struct wlr_cursor *cur,
		struct wlr_input_device *dev) {
	if (dev->type != WLR_INPUT_DEVICE_POINTER &&
			dev->type != WLR_INPUT_DEVICE_TOUCH &&
			dev->type != WLR_INPUT_DEVICE_TABLET_TOOL) {
		wlr_log(L_ERROR, "only device types of pointer, touch or tablet tool are"
				"supported");
		return;
	}

	// TODO support other device types
	if (dev->type != WLR_INPUT_DEVICE_POINTER) {
		wlr_log(L_ERROR, "TODO: support touch and tablet tool devices");
		return;
	}

	// make sure it is not already attached
	struct wlr_cursor_device *_dev;
	wl_list_for_each(_dev, &cur->state->devices, link) {
		if (_dev->device == dev) {
			return;
		}
	}

	struct wlr_cursor_device *device = calloc(1, sizeof(struct wlr_cursor_device));
	if (!device) {
		wlr_log(L_ERROR, "Failed to allocate wlr_cursor_device");
		return;
	}

	device->cursor = cur;
	device->device = dev;

	// listen to events
	wl_signal_add(&dev->pointer->events.motion, &device->motion);
	device->motion.notify = handle_pointer_motion;

	wl_signal_add(&dev->pointer->events.motion_absolute, &device->motion_absolute);
	device->motion_absolute.notify = handle_pointer_motion_absolute;

	wl_signal_add(&dev->pointer->events.button, &device->button);
	device->button.notify = handle_pointer_button;

	wl_signal_add(&dev->pointer->events.axis, &device->axis);
	device->axis.notify = handle_pointer_axis;

	wl_list_insert(&cur->state->devices, &device->link);
}

void wlr_cursor_detach_input_device(struct wlr_cursor *cur,
		struct wlr_input_device *dev) {
	struct wlr_cursor_device *target_device = NULL, *_device = NULL;
	wl_list_for_each(_device, &cur->state->devices, link) {
		if (_device->device == dev) {
			target_device = _device;
			break;
		}
	}

	if (target_device) {
		wl_list_remove(&target_device->link);
		free(target_device);
	}
}

void wlr_cursor_attach_output_layout(struct wlr_cursor *cur,
		struct wlr_output_layout *l) {
	cur->state->layout = l;
}

void wlr_cursor_map_to_output(struct wlr_cursor *cur,
		struct wlr_output *output) {
	wlr_log(L_DEBUG, "TODO: map to output");
}

void wlr_cursor_map_input_to_output(struct wlr_cursor *cur,
		struct wlr_input_device *dev, struct wlr_output *output) {
	wlr_log(L_DEBUG, "TODO map input to output");
}
