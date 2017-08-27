#include <wlr/types/wlr_cursor.h>
#include <stdlib.h>
#include <assert.h>
#include <limits.h>
#include <wlr/util/log.h>
#include <wayland-server.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_input_device.h>

struct wlr_cursor_device {
	struct wlr_cursor *cursor;
	struct wlr_input_device *device;
	struct wl_list link;
	struct wlr_output *mapped_output;
	struct wlr_geometry *mapped_geometry;

	struct wl_listener motion;
	struct wl_listener motion_absolute;
	struct wl_listener button;
	struct wl_listener axis;

	struct wl_listener touch_down;
	struct wl_listener touch_up;
	struct wl_listener touch_motion;
	struct wl_listener touch_cancel;
};

struct wlr_cursor_state {
	struct wl_list devices;
	struct wlr_output_layout *layout;
	struct wlr_xcursor *xcursor;
	struct wlr_output *mapped_output;
	struct wlr_geometry *mapped_geometry;
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

	cur->state->mapped_output = NULL;

	wl_list_init(&cur->state->devices);

	// pointer signals
	wl_signal_init(&cur->events.motion);
	wl_signal_init(&cur->events.motion_absolute);
	wl_signal_init(&cur->events.button);
	wl_signal_init(&cur->events.axis);

	// touch signals
	wl_signal_init(&cur->events.touch_up);
	wl_signal_init(&cur->events.touch_down);
	wl_signal_init(&cur->events.touch_motion);
	wl_signal_init(&cur->events.touch_cancel);

	cur->x = 100;
	cur->y = 100;

	return cur;
}

void wlr_cursor_destroy(struct wlr_cursor *cur) {
	struct wlr_cursor_device *device, *device_tmp = NULL;
	wl_list_for_each_safe(device, device_tmp, &cur->state->devices, link) {
		wl_list_remove(&device->link);
		free(device);
	}

	free(cur);
}

void wlr_cursor_set_xcursor(struct wlr_cursor *cur, struct wlr_xcursor *xcur) {
	cur->state->xcursor = xcur;
}

static struct wlr_cursor_device *get_cursor_device(struct wlr_cursor *cur,
		struct wlr_input_device *device) {
	struct wlr_cursor_device *c_device, *ret = NULL;
	wl_list_for_each(c_device, &cur->state->devices, link) {
		if (c_device->device == device) {
			ret = c_device;
			break;
		}
	}

	return ret;
}

static void wlr_cursor_warp_unchecked(struct wlr_cursor *cur, double x, double y) {
	int hotspot_x = 0;
	int hotspot_y = 0;

	if (cur->state->xcursor && cur->state->xcursor->image_count > 0) {
		struct wlr_xcursor_image *image = cur->state->xcursor->images[0];
		hotspot_x = image->hotspot_x;
		hotspot_y = image->hotspot_y;
	}


	struct wlr_output_layout_output *l_output;
	wl_list_for_each(l_output, &cur->state->layout->outputs, link) {
		double output_x = x;
		double output_y = y;

		wlr_output_layout_output_coords(cur->state->layout,
			l_output->output, &output_x, &output_y);
		wlr_output_move_cursor(l_output->output, output_x - hotspot_x,
			output_y - hotspot_y);
	}
}

bool wlr_cursor_warp(struct wlr_cursor *cur, struct wlr_input_device *dev,
		double x, double y) {
	assert(cur->state->layout);
	struct wlr_output *output;
	output = wlr_output_layout_output_at(cur->state->layout, x, y);

	struct wlr_output *mapped_output = NULL;
	struct wlr_cursor_device *c_device = get_cursor_device(cur, dev);

	if (c_device && c_device->mapped_output) {
		mapped_output = c_device->mapped_output;
	} else {
		mapped_output = cur->state->mapped_output;
	}

	if (!output) {
		return false;
	}

	if (mapped_output &&
			!wlr_output_layout_contains_point(cur->state->layout, mapped_output,
				x, y)) {
		return false;
	}

	wlr_cursor_warp_unchecked(cur, x, y);
	return true;
}

void wlr_cursor_warp_absolute(struct wlr_cursor *cur,
		struct wlr_input_device *dev, double x_mm, double y_mm) {
	// convert from absolute to global coordinates
	assert(cur->state->layout);
	struct wlr_output *mapped_output = NULL;
	struct wlr_cursor_device *c_device = get_cursor_device(cur, dev);

	if (c_device && c_device->mapped_output) {
		mapped_output = c_device->mapped_output;
	} else {
		mapped_output = cur->state->mapped_output;
	}

	struct wlr_geometry *constraints = calloc(1, sizeof(struct wlr_geometry));
	// XXX how do we express infinite regions?
	constraints->x = INT_MIN / 2;
	constraints->y = INT_MIN / 2;
	constraints->width = INT_MAX;
	constraints->height = INT_MAX;

	if (cur->state->mapped_geometry) {
		wlr_geometry_intersection(cur->state->mapped_geometry, constraints,
			&constraints);
	}
	if (c_device->mapped_geometry) {
		wlr_geometry_intersection(c_device->mapped_geometry, constraints,
			&constraints);
	}
	struct wlr_geometry *geo;
	if (mapped_output) {
		geo = wlr_output_layout_get_geometry(cur->state->layout, mapped_output);
		wlr_geometry_intersection(geo, constraints, &constraints);
	}
	geo = wlr_output_layout_get_geometry(cur->state->layout, NULL);
	wlr_geometry_intersection(geo, constraints, &constraints);

	if (wlr_geometry_empty(constraints)) {
		goto out;
	}

	double x = constraints->width * x_mm + constraints->x;
	double y = constraints->height * y_mm + constraints->y;

	wlr_cursor_warp_unchecked(cur, x, y);

out:
	if (constraints) {
		free(constraints);
	}
}

void wlr_cursor_move(struct wlr_cursor *cur, struct wlr_input_device *dev,
		double delta_x, double delta_y) {
	assert(cur->state->layout);
	struct wlr_output *mapped_output = NULL;
	struct wlr_cursor_device *c_device = get_cursor_device(cur, dev);

	if (c_device && c_device->mapped_output) {
		mapped_output = c_device->mapped_output;
	} else {
		mapped_output = cur->state->mapped_output;
	}

	double x = cur->x + delta_x;
	double y = cur->y + delta_y;

	// geometry constraints
	struct wlr_geometry *constraints = NULL;
	if (cur->state->mapped_geometry != NULL ||
			c_device->mapped_geometry != NULL) {
		constraints = calloc(1, sizeof(struct wlr_geometry));

		if (!wlr_geometry_intersection(cur->state->mapped_geometry,
				c_device->mapped_geometry, &constraints)) {
			// TODO handle no possible movement
			goto out;
		}

		int closest_x, closest_y;
		wlr_geometry_closest_boundary(constraints, x, y,
			&closest_x, &closest_y, NULL);
		x = closest_x;
		y = closest_y;
	}

	// layout constraints
	struct wlr_output *output;
	output = wlr_output_layout_output_at(cur->state->layout, x, y);

	if (!output || (mapped_output && mapped_output != output)) {
		double closest_x, closest_y;
		wlr_output_layout_closest_boundary(cur->state->layout, mapped_output, x,
			y, &closest_x, &closest_y);
		x = closest_x;
		y = closest_y;
	}

	if (constraints && !wlr_geometry_contains_point(constraints, x, y)) {
		// TODO handle no possible movement
		goto out;
	}

	wlr_cursor_warp_unchecked(cur, x, y);
	cur->x = x;
	cur->y = y;

out:
	if (constraints) {
		free(constraints);
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

static void handle_touch_up(struct wl_listener *listener, void *data) {
	struct wlr_event_touch_up *event = data;
	struct wlr_cursor_device *device;
	device = wl_container_of(listener, device, touch_up);
	wl_signal_emit(&device->cursor->events.touch_up, event);
}

static void handle_touch_down(struct wl_listener *listener, void *data) {
	struct wlr_event_touch_down *event = data;
	struct wlr_cursor_device *device;
	device = wl_container_of(listener, device, touch_down);
	wl_signal_emit(&device->cursor->events.touch_down, event);
}

static void handle_touch_motion(struct wl_listener *listener, void *data) {
	struct wlr_event_touch_motion *event = data;
	struct wlr_cursor_device *device;
	device = wl_container_of(listener, device, touch_motion);
	wl_signal_emit(&device->cursor->events.touch_motion, event);
}

static void handle_touch_cancel(struct wl_listener *listener, void *data) {
	struct wlr_event_touch_cancel *event = data;
	struct wlr_cursor_device *device;
	device = wl_container_of(listener, device, touch_cancel);
	wl_signal_emit(&device->cursor->events.touch_cancel, event);
}

void wlr_cursor_attach_input_device(struct wlr_cursor *cur,
		struct wlr_input_device *dev) {
	if (dev->type != WLR_INPUT_DEVICE_POINTER &&
			dev->type != WLR_INPUT_DEVICE_TOUCH &&
			dev->type != WLR_INPUT_DEVICE_TABLET_TOOL) {
		wlr_log(L_ERROR, "only device types of pointer, touch or tablet tool"
				"are supported");
		return;
	}

	// TODO support other device types
	if (dev->type != WLR_INPUT_DEVICE_POINTER &&
			dev->type != WLR_INPUT_DEVICE_TOUCH) {
		wlr_log(L_ERROR, "TODO: support tablet tool devices");
		return;
	}

	// make sure it is not already attached
	struct wlr_cursor_device *_dev;
	wl_list_for_each(_dev, &cur->state->devices, link) {
		if (_dev->device == dev) {
			return;
		}
	}

	struct wlr_cursor_device *device;
	device = calloc(1, sizeof(struct wlr_cursor_device));
	if (!device) {
		wlr_log(L_ERROR, "Failed to allocate wlr_cursor_device");
		return;
	}

	device->cursor = cur;
	device->device = dev;

	// listen to events

	if (dev->type == WLR_INPUT_DEVICE_POINTER) {
		wl_signal_add(&dev->pointer->events.motion, &device->motion);
		device->motion.notify = handle_pointer_motion;

		wl_signal_add(&dev->pointer->events.motion_absolute, &device->motion_absolute);
		device->motion_absolute.notify = handle_pointer_motion_absolute;

		wl_signal_add(&dev->pointer->events.button, &device->button);
		device->button.notify = handle_pointer_button;

		wl_signal_add(&dev->pointer->events.axis, &device->axis);
		device->axis.notify = handle_pointer_axis;
	} else if (dev->type == WLR_INPUT_DEVICE_TOUCH) {
		wl_signal_add(&dev->touch->events.motion, &device->touch_motion);
		device->touch_motion.notify = handle_touch_motion;

		wl_signal_add(&dev->touch->events.down, &device->touch_down);
		device->touch_down.notify = handle_touch_down;

		wl_signal_add(&dev->touch->events.up, &device->touch_up);
		device->touch_up.notify = handle_touch_up;

		wl_signal_add(&dev->touch->events.cancel, &device->touch_cancel);
		device->touch_cancel.notify = handle_touch_cancel;
	}

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
	cur->state->mapped_output = output;
}

void wlr_cursor_map_input_to_output(struct wlr_cursor *cur,
		struct wlr_input_device *dev, struct wlr_output *output) {
	struct wlr_cursor_device *c_device = get_cursor_device(cur, dev);
	if (!c_device) {
		wlr_log(L_ERROR, "Cannot map device \"%s\" to output (not found in this cursor)", dev->name);
		return;
	}

	c_device->mapped_output = output;
}

void wlr_cursor_map_to_region(struct wlr_cursor *cur, struct wlr_geometry *geo) {
	cur->state->mapped_geometry = geo;
}

void wlr_cursor_map_input_to_region(struct wlr_cursor *cur,
		struct wlr_input_device *dev, struct wlr_geometry *geo) {
	struct wlr_cursor_device *c_device = get_cursor_device(cur, dev);
	if (!c_device) {
		wlr_log(L_ERROR, "Cannot map device \"%s\" to geometry (not found in this cursor)", dev->name);
		return;
	}
	c_device->mapped_geometry = geo;
}
