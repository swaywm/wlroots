#include <stdint.h>
#include <string.h>
#include <wayland-server.h>
#include <wlr/types/wlr_cursor.h>
#include "rootston/config.h"
#include "rootston/input.h"
#include "rootston/desktop.h"

void cursor_update_position(struct roots_input *input, uint32_t time) {
	/*
	if (input->motion_context.surface) {
		struct example_xdg_surface_v6 *surface;
		surface = sample->motion_context.surface;
		surface->position.lx = sample->cursor->x - sample->motion_context.off_x;
		surface->position.ly = sample->cursor->y - sample->motion_context.off_y;
		return;
	}

	struct wlr_xdg_surface_v6 *surface = example_xdg_surface_at(sample,
			sample->cursor->x, sample->cursor->y);

	if (surface) {
		struct example_xdg_surface_v6 *esurface = surface->data;

		double sx = sample->cursor->x - esurface->position.lx;
		double sy = sample->cursor->y - esurface->position.ly;

		// TODO z-order
		wlr_seat_pointer_enter(sample->wl_seat, surface->surface, sx, sy);
		wlr_seat_pointer_send_motion(sample->wl_seat, time, sx, sy);
	} else {
		wlr_seat_pointer_clear_focus(sample->wl_seat);
	}
	*/
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

static void handle_cursor_button(struct wl_listener *listener, void *data) {
	/* TODO
	struct sample_state *sample =
		wl_container_of(listener, sample, cursor_button);
	struct wlr_event_pointer_button *event = data;

	struct wlr_xdg_surface_v6 *surface =
		example_xdg_surface_at(sample, sample->cursor->x, sample->cursor->y);

	uint32_t serial = wlr_seat_pointer_send_button(sample->wl_seat,
			(uint32_t)event->time_usec, event->button, event->state);

	int i;
	switch (event->state) {
	case WLR_BUTTON_RELEASED:
		if (sample->motion_context.surface) {
			sample->motion_context.surface = NULL;
		}
		break;
	case WLR_BUTTON_PRESSED:
		i = sample->input_cache_idx;
		sample->input_cache[i].serial = serial;
		sample->input_cache[i].cursor = sample->cursor;
		sample->input_cache[i].device = event->device;
		sample->input_cache_idx = (i + 1)
			% (sizeof(sample->input_cache) / sizeof(sample->input_cache[0]));
		example_set_focused_surface(sample, surface);
		wlr_log(L_DEBUG, "Stored event %d at %d", serial, i);
		if (sample->mod_down && event->button == BTN_LEFT) {
			struct example_xdg_surface_v6 *esurface = surface->data;
			sample->motion_context.surface = esurface;
			sample->motion_context.off_x = sample->cursor->x - esurface->position.lx;
			sample->motion_context.off_y = sample->cursor->y - esurface->position.ly;
			wlr_seat_pointer_clear_focus(sample->wl_seat);
		}
		break;
	}
	*/
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
	/* TODO
	struct roots_input *input = wl_container_of(listener, input, tool_tip);
	struct wlr_event_tablet_tool_tip *event = data;

	struct wlr_xdg_surface_v6 *surface =
		example_xdg_surface_at(sample, sample->cursor->x, sample->cursor->y);
	example_set_focused_surface(sample, surface);

	wlr_seat_pointer_send_button(sample->wl_seat, (uint32_t)event->time_usec,
		BTN_LEFT, event->state);
	*/
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
