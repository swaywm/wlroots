#define _POSIX_C_SOURCE 199309L
#define _XOPEN_SOURCE 500
#include <assert.h>
#include <GLES2/gl2.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <wayland-server-protocol.h>
#include <wayland-server.h>
#include <wlr/backend.h>
#include <wlr/backend/session.h>
#include <wlr/render/gles2.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_list.h>
#include <wlr/types/wlr_matrix.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/util/log.h>
#include <wlr/xcursor.h>
#include <xkbcommon/xkbcommon.h>
#include "support/cat.h"
#include "support/config.h"
#include "support/shared.h"

struct sample_state {
	struct compositor_state *compositor;
	struct example_config *config;
	struct wlr_xcursor *xcursor;
	struct wlr_cursor *cursor;
	double cur_x, cur_y;
	float default_color[4];
	float clear_color[4];
	struct wlr_output_layout *layout;
	struct wl_list devices;

	struct wl_listener cursor_motion;
	struct wl_listener cursor_motion_absolute;
	struct wl_listener cursor_button;
	struct wl_listener cursor_axis;

	struct wl_listener touch_motion;
	struct wl_listener touch_up;
	struct wl_listener touch_down;
	struct wl_listener touch_cancel;
	struct wl_list touch_points;

	struct wl_listener tablet_tool_axis;
	struct wl_listener tablet_tool_proxmity;
	struct wl_listener tablet_tool_tip;
	struct wl_listener tablet_tool_button;
};

struct touch_point {
	int32_t touch_id;
	double x, y;
	struct wl_list link;
};

static void warp_to_touch(struct sample_state *sample,
		struct wlr_input_device *dev) {
	if (wl_list_empty(&sample->touch_points)) {
		return;
	}

	double x = 0, y = 0;
	size_t n = 0;
	struct touch_point *point;
	wl_list_for_each(point, &sample->touch_points, link) {
		x += point->x;
		y += point->y;
		n++;
	}
	x /= n;
	y /= n;
	wlr_cursor_warp_absolute(sample->cursor, dev, x, y);
}

static void handle_output_frame(struct output_state *output,
		struct timespec *ts) {
	struct compositor_state *state = output->compositor;
	struct sample_state *sample = state->data;
	struct wlr_output *wlr_output = output->output;

	wlr_output_make_current(wlr_output, NULL);

	glClearColor(sample->clear_color[0], sample->clear_color[1],
		sample->clear_color[2], sample->clear_color[3]);
	glClear(GL_COLOR_BUFFER_BIT);

	wlr_output_swap_buffers(wlr_output, NULL, NULL);
}

static void handle_output_add(struct output_state *ostate) {
	struct sample_state *sample = ostate->compositor->data;

	struct output_config *o_config =
		example_config_get_output(sample->config, ostate->output);

	if (o_config) {
		wlr_output_set_transform(ostate->output, o_config->transform);
		wlr_output_layout_add(sample->layout, ostate->output, o_config->x,
			o_config->y);
	} else {
		wlr_output_layout_add_auto(sample->layout, ostate->output);
	}

	example_config_configure_cursor(sample->config, sample->cursor,
		sample->compositor);

	struct wlr_xcursor_image *image = sample->xcursor->images[0];
	wlr_cursor_set_image(sample->cursor, image->buffer, image->width * 4,
		image->width, image->height, image->hotspot_x, image->hotspot_y, 0);

	wlr_cursor_warp(sample->cursor, NULL, sample->cursor->x, sample->cursor->y);
}

static void handle_output_remove(struct output_state *ostate) {
	struct sample_state *sample = ostate->compositor->data;

	wlr_output_layout_remove(sample->layout, ostate->output);

	example_config_configure_cursor(sample->config, sample->cursor,
		sample->compositor);
}

static void handle_input_add(struct compositor_state *state,
		struct wlr_input_device *device) {
	struct sample_state *sample = state->data;

	if (device->type == WLR_INPUT_DEVICE_POINTER ||
			device->type == WLR_INPUT_DEVICE_TOUCH ||
			device->type == WLR_INPUT_DEVICE_TABLET_TOOL) {
		wlr_cursor_attach_input_device(sample->cursor, device);
		example_config_configure_cursor(sample->config, sample->cursor,
			sample->compositor);
	}
}

static void handle_cursor_motion(struct wl_listener *listener, void *data) {
	struct sample_state *sample =
		wl_container_of(listener, sample, cursor_motion);
	struct wlr_event_pointer_motion *event = data;
	wlr_cursor_move(sample->cursor, event->device, event->delta_x,
			event->delta_y);
}

static void handle_cursor_motion_absolute(struct wl_listener *listener,
		void *data) {
	struct sample_state *sample =
		wl_container_of(listener, sample, cursor_motion_absolute);
	struct wlr_event_pointer_motion_absolute *event = data;

	sample->cur_x = event->x;
	sample->cur_y = event->y;

	wlr_cursor_warp_absolute(sample->cursor, event->device, sample->cur_x,
		sample->cur_y);
}

static void handle_cursor_button(struct wl_listener *listener, void *data) {
	struct sample_state *sample =
		wl_container_of(listener, sample, cursor_button);
	struct wlr_event_pointer_button *event = data;

	float (*color)[4];
	if (event->state == WLR_BUTTON_RELEASED) {
		color = &sample->default_color;
		memcpy(&sample->clear_color, color, sizeof(*color));
	} else {
		float red[4] = { 0.25f, 0.25f, 0.25f, 1 };
		red[event->button % 3] = 1;
		color = &red;
		memcpy(&sample->clear_color, color, sizeof(*color));
	}
}

static void handle_cursor_axis(struct wl_listener *listener, void *data) {
	struct sample_state *sample =
		wl_container_of(listener, sample, cursor_axis);
	struct wlr_event_pointer_axis *event = data;

	for (size_t i = 0; i < 3; ++i) {
		sample->default_color[i] += event->delta > 0 ? -0.05f : 0.05f;
		if (sample->default_color[i] > 1.0f) {
			sample->default_color[i] = 1.0f;
		}
		if (sample->default_color[i] < 0.0f) {
			sample->default_color[i] = 0.0f;
		}
	}

	memcpy(&sample->clear_color, &sample->default_color,
			sizeof(sample->clear_color));
}

static void handle_touch_up(struct wl_listener *listener, void *data) {
	struct sample_state *sample = wl_container_of(listener, sample, touch_up);
	struct wlr_event_touch_up *event = data;

	struct touch_point *point, *tmp;
	wl_list_for_each_safe(point, tmp, &sample->touch_points, link) {
		if (point->touch_id == event->touch_id) {
			wl_list_remove(&point->link);
			break;
		}
	}

	warp_to_touch(sample, event->device);
}

static void handle_touch_down(struct wl_listener *listener, void *data) {
	struct sample_state *sample = wl_container_of(listener, sample, touch_down);
	struct wlr_event_touch_down *event = data;
	struct touch_point *point = calloc(1, sizeof(struct touch_point));
	point->touch_id = event->touch_id;
	point->x = event->x;
	point->y = event->y;
	wl_list_insert(&sample->touch_points, &point->link);

	warp_to_touch(sample, event->device);
}

static void handle_touch_motion(struct wl_listener *listener, void *data) {
	struct sample_state *sample =
		wl_container_of(listener, sample, touch_motion);
	struct wlr_event_touch_motion *event = data;

	struct touch_point *point;
	wl_list_for_each(point, &sample->touch_points, link) {
		if (point->touch_id == event->touch_id) {
			point->x = event->x;
			point->y = event->y;
			break;
		}
	}

	warp_to_touch(sample, event->device);
}

static void handle_touch_cancel(struct wl_listener *listener, void *data) {
	wlr_log(L_DEBUG, "TODO: touch cancel");
}

static void handle_tablet_tool_axis(struct wl_listener *listener, void *data) {
	struct sample_state *sample =
		wl_container_of(listener, sample, tablet_tool_axis);
	struct wlr_event_tablet_tool_axis *event = data;
	if ((event->updated_axes & WLR_TABLET_TOOL_AXIS_X) &&
			(event->updated_axes & WLR_TABLET_TOOL_AXIS_Y)) {
		wlr_cursor_warp_absolute(sample->cursor, event->device,
				event->x_mm / event->width_mm, event->y_mm / event->height_mm);
	}
}

int main(int argc, char *argv[]) {
	wlr_log_init(L_DEBUG, NULL);
	struct sample_state state = {
		.default_color = { 0.25f, 0.25f, 0.25f, 1 },
		.clear_color = { 0.25f, 0.25f, 0.25f, 1 },
	};

	state.config = parse_args(argc, argv);
	state.cursor = wlr_cursor_create();
	state.layout = wlr_output_layout_create();
	wlr_cursor_attach_output_layout(state.cursor, state.layout);
	wlr_cursor_map_to_region(state.cursor, state.config->cursor.mapped_box);
	wl_list_init(&state.devices);
	wl_list_init(&state.touch_points);

	// pointer events
	wl_signal_add(&state.cursor->events.motion, &state.cursor_motion);
	state.cursor_motion.notify = handle_cursor_motion;

	wl_signal_add(&state.cursor->events.motion_absolute,
		&state.cursor_motion_absolute);
	state.cursor_motion_absolute.notify = handle_cursor_motion_absolute;

	wl_signal_add(&state.cursor->events.button, &state.cursor_button);
	state.cursor_button.notify = handle_cursor_button;

	wl_signal_add(&state.cursor->events.axis, &state.cursor_axis);
	state.cursor_axis.notify = handle_cursor_axis;

	// touch events
	wl_signal_add(&state.cursor->events.touch_up, &state.touch_up);
	state.touch_up.notify = handle_touch_up;

	wl_signal_add(&state.cursor->events.touch_down, &state.touch_down);
	state.touch_down.notify = handle_touch_down;

	wl_signal_add(&state.cursor->events.touch_motion, &state.touch_motion);
	state.touch_motion.notify = handle_touch_motion;

	wl_signal_add(&state.cursor->events.touch_cancel, &state.touch_cancel);
	state.touch_cancel.notify = handle_touch_cancel;

	// tool events
	wl_signal_add(&state.cursor->events.tablet_tool_axis,
		&state.tablet_tool_axis);
	state.tablet_tool_axis.notify = handle_tablet_tool_axis;

	struct compositor_state compositor = { 0 };
	compositor.data = &state;
	compositor.output_add_cb = handle_output_add;
	compositor.output_remove_cb = handle_output_remove;
	compositor.output_frame_cb = handle_output_frame;
	compositor.input_add_cb = handle_input_add;

	state.compositor = &compositor;

	struct wlr_xcursor_theme *theme = wlr_xcursor_theme_load("default", 16);
	if (!theme) {
		wlr_log(L_ERROR, "Failed to load cursor theme");
		return 1;
	}
	state.xcursor = wlr_xcursor_theme_get_cursor(theme, "left_ptr");
	if (!state.xcursor) {
		wlr_log(L_ERROR, "Failed to load left_ptr cursor");
		return 1;
	}

	struct wlr_xcursor_image *image = state.xcursor->images[0];
	wlr_cursor_set_image(state.cursor, image->buffer, image->width * 4,
		image->width, image->height, image->hotspot_x, image->hotspot_y, 0);

	compositor_init(&compositor);
	if (!wlr_backend_start(compositor.backend)) {
		wlr_log(L_ERROR, "Failed to start backend");
		wlr_backend_destroy(compositor.backend);
		exit(1);
	}
	wl_display_run(compositor.display);
	compositor_fini(&compositor);

	wlr_xcursor_theme_destroy(theme);
	example_config_destroy(state.config);
	wlr_cursor_destroy(state.cursor);
	wlr_output_layout_destroy(state.layout);
}
