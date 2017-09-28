#include <stdint.h>
#include <string.h>
// TODO: BSD et al
#include <linux/input-event-codes.h>
#include <wayland-server.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/util/log.h>
#include "rootston/config.h"
#include "rootston/input.h"
#include "rootston/desktop.h"

const struct roots_input_event *get_input_event(struct roots_input *input,
		uint32_t serial) {
	size_t len = sizeof(input->input_events) / sizeof(*input->input_events);
	for (size_t i = 0; i < len; ++i) {
		if (input->input_events[i].cursor
				&& input->input_events[i].serial == serial) {
			return &input->input_events[i];
		}
	}
	return NULL;
}

void view_begin_move(struct roots_input *input, struct wlr_cursor *cursor,
		struct roots_view *view) {
	input->mode = ROOTS_CURSOR_MOVE;
	input->offs_x = cursor->x - view->x;
	input->offs_y = cursor->y - view->y;
	wlr_seat_pointer_clear_focus(input->wl_seat);
}

void cursor_update_position(struct roots_input *input, uint32_t time) {
	struct roots_desktop *desktop = input->server->desktop;
	struct roots_view *view;
	switch (input->mode) {
	case ROOTS_CURSOR_PASSTHROUGH:
		view = view_at(desktop, input->cursor->x, input->cursor->y);
		if (view) {
			struct wlr_box box;
			view_get_input_bounds(view, &box);
			double sx = input->cursor->x - view->x;
			double sy = input->cursor->y - view->y;
			wlr_seat_pointer_enter(input->wl_seat, view->wlr_surface, sx, sy);
			wlr_seat_pointer_send_motion(input->wl_seat, time, sx, sy);
		} else {
			wlr_seat_pointer_clear_focus(input->wl_seat);
		}
		break;
	case ROOTS_CURSOR_MOVE:
		if (input->active_view) {
			input->active_view->x = input->cursor->x - input->offs_x;
			input->active_view->y = input->cursor->y - input->offs_y;
		}
		break;
	case ROOTS_CURSOR_RESIZE:
		break;
	case ROOTS_CURSOR_ROTATE:
		break;
	}
}

static void set_view_focus(struct roots_input *input,
		struct roots_desktop *desktop, struct roots_view *view) {
	if (input->active_view == view) {
		return;
	}
	size_t index = 0;
	for (size_t i = 0; i < desktop->views->length; ++i) {
		struct roots_view *_view = desktop->views->items[i];
		view_activate(_view, _view == view);
		if (view == _view) {
			index = i;
		}
	}
	input->active_view = view;
	input->mode = ROOTS_CURSOR_PASSTHROUGH;
	// TODO: list_swap
	list_del(desktop->views, index);
	list_add(desktop->views, view);
}

static void handle_cursor_motion(struct wl_listener *listener, void *data) {
	struct roots_input *input = wl_container_of(listener, input, cursor_motion);
	struct wlr_event_pointer_motion *event = data;
	wlr_cursor_move(input->cursor, event->device,
			event->delta_x, event->delta_y);
	cursor_update_position(input, (uint32_t)event->time_usec);
}

static void handle_cursor_motion_absolute(struct wl_listener *listener,
		void *data) {
	struct roots_input *input = wl_container_of(listener,
			input, cursor_motion_absolute);
	struct wlr_event_pointer_motion_absolute *event = data;
	wlr_cursor_warp_absolute(input->cursor, event->device,
		event->x_mm / event->width_mm, event->y_mm / event->height_mm);
	cursor_update_position(input, (uint32_t)event->time_usec);
}

static void handle_cursor_axis(struct wl_listener *listener, void *data) {
	struct roots_input *input =
		wl_container_of(listener, input, cursor_axis);
	struct wlr_event_pointer_axis *event = data;
	wlr_seat_pointer_send_axis(input->wl_seat, event->time_sec,
		event->orientation, event->delta);
}

static void do_cursor_button_press(struct roots_input *input,
		struct wlr_cursor *cursor, struct wlr_input_device *device,
		uint32_t time, uint32_t button, uint32_t state) {
	struct roots_desktop *desktop = input->server->desktop;
	struct roots_view *view = view_at(desktop,
			input->cursor->x, input->cursor->y);
	uint32_t serial = wlr_seat_pointer_send_button(
			input->wl_seat, time, button, state);
	int i;
	switch (state) {
	case WLR_BUTTON_RELEASED:
		input->active_view = NULL;
		input->mode = ROOTS_CURSOR_PASSTHROUGH;
		break;
	case WLR_BUTTON_PRESSED:
		i = input->input_events_idx;
		input->input_events[i].serial = serial;
		input->input_events[i].cursor = cursor;
		input->input_events[i].device = device;
		input->input_events_idx = (i + 1)
			% (sizeof(input->input_events) / sizeof(input->input_events[0]));
		set_view_focus(input, desktop, view);
		if (view) {
			wlr_seat_keyboard_enter(input->wl_seat, view->wlr_surface);
		}
		break;
	}
}

static void handle_cursor_button(struct wl_listener *listener, void *data) {
	struct roots_input *input = wl_container_of(listener, input, cursor_button);
	struct wlr_event_pointer_button *event = data;
	do_cursor_button_press(input, input->cursor, event->device,
			(uint32_t)event->time_usec, event->button, event->state);
}

static void handle_tool_axis(struct wl_listener *listener, void *data) {
	struct roots_input *input = wl_container_of(listener, input, cursor_tool_axis);
	struct wlr_event_tablet_tool_axis *event = data;
	if ((event->updated_axes & WLR_TABLET_TOOL_AXIS_X) &&
			(event->updated_axes & WLR_TABLET_TOOL_AXIS_Y)) {
		wlr_cursor_warp_absolute(input->cursor, event->device,
			event->x_mm / event->width_mm, event->y_mm / event->height_mm);
		cursor_update_position(input, (uint32_t)event->time_usec);
	}
}

static void handle_tool_tip(struct wl_listener *listener, void *data) {
	struct roots_input *input = wl_container_of(listener, input, cursor_tool_tip);
	struct wlr_event_tablet_tool_tip *event = data;
	do_cursor_button_press(input, input->cursor, event->device,
			(uint32_t)event->time_usec, BTN_LEFT, event->state);
}

void cursor_initialize(struct roots_input *input) {
	struct wlr_cursor *cursor = input->cursor;

	wl_list_init(&input->cursor_motion.link);
	wl_signal_add(&cursor->events.motion, &input->cursor_motion);
	input->cursor_motion.notify = handle_cursor_motion;

	wl_list_init(&input->cursor_motion_absolute.link);
	wl_signal_add(&cursor->events.motion_absolute,
		&input->cursor_motion_absolute);
	input->cursor_motion_absolute.notify = handle_cursor_motion_absolute;

	wl_list_init(&input->cursor_button.link);
	wl_signal_add(&cursor->events.button, &input->cursor_button);
	input->cursor_button.notify = handle_cursor_button;

	wl_list_init(&input->cursor_axis.link);
	wl_signal_add(&cursor->events.axis, &input->cursor_axis);
	input->cursor_axis.notify = handle_cursor_axis;

	wl_list_init(&input->cursor_tool_axis.link);
	wl_signal_add(&cursor->events.tablet_tool_axis, &input->cursor_tool_axis);
	input->cursor_tool_axis.notify = handle_tool_axis;

	wl_list_init(&input->cursor_tool_tip.link);
	wl_signal_add(&cursor->events.tablet_tool_tip, &input->cursor_tool_tip);
	input->cursor_tool_tip.notify = handle_tool_tip;
}

static void reset_device_mappings(struct roots_config *config,
		struct wlr_cursor *cursor, struct wlr_input_device *device) {
	wlr_cursor_map_input_to_output(cursor, device, NULL);
	struct device_config *dconfig;
	if ((dconfig = config_get_device(config, device))) {
		wlr_cursor_map_input_to_region(cursor, device, dconfig->mapped_box);
	}
}

static void set_device_output_mappings(struct roots_config *config,
		struct wlr_cursor *cursor, struct wlr_output *output,
		struct wlr_input_device *device) {
	struct device_config *dconfig;
	dconfig = config_get_device(config, device);
	if (dconfig && dconfig->mapped_output &&
			strcmp(dconfig->mapped_output, output->name) == 0) {
		wlr_cursor_map_input_to_output(cursor, device, output);
	}
}

void cursor_load_config(struct roots_config *config,
		struct wlr_cursor *cursor,
		struct roots_input *input,
		struct roots_desktop *desktop) {
	struct roots_pointer *pointer;
	struct roots_touch *touch;
	struct roots_tablet_tool *tablet_tool;
	struct roots_output *output;

	// reset mappings
	wlr_cursor_map_to_output(cursor, NULL);
	wl_list_for_each(pointer, &input->pointers, link) {
		reset_device_mappings(config, cursor, pointer->device);
	}
	wl_list_for_each(touch, &input->touch, link) {
		reset_device_mappings(config, cursor, touch->device);
	}
	wl_list_for_each(tablet_tool, &input->tablet_tools, link) {
		reset_device_mappings(config, cursor, tablet_tool->device);
	}

	// configure device to output mappings
	const char *mapped_output = config->cursor.mapped_output;
	wl_list_for_each(output, &desktop->outputs, link) {
		if (mapped_output && strcmp(mapped_output, output->wlr_output->name) == 0) {
			wlr_cursor_map_to_output(cursor, output->wlr_output);
		}

		wl_list_for_each(pointer, &input->pointers, link) {
			set_device_output_mappings(config, cursor, output->wlr_output,
				pointer->device);
		}
		wl_list_for_each(tablet_tool, &input->tablet_tools, link) {
			set_device_output_mappings(config, cursor, output->wlr_output,
				tablet_tool->device);
		}
		wl_list_for_each(touch, &input->touch, link) {
			set_device_output_mappings(config, cursor, output->wlr_output,
				touch->device);
		}
	}
}
