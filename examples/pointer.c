#define _POSIX_C_SOURCE 199309L
#define _XOPEN_SOURCE 500
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <unistd.h>
#include <math.h>
#include <assert.h>
#include <wayland-server.h>
#include <wayland-server-protocol.h>
#include <xkbcommon/xkbcommon.h>
#include <GLES2/gl2.h>
#include <wlr/render/matrix.h>
#include <wlr/render/gles2.h>
#include <wlr/render.h>
#include <wlr/backend.h>
#include <wlr/backend/session.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/xcursor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/util/log.h>
#include "shared.h"
#include "cat.h"

struct sample_state {
	struct compositor_state *compositor;
	struct example_config *config;
	struct wlr_xcursor *xcursor;
	struct wlr_cursor *cursor;
	double cur_x, cur_y;
	float default_color[4];
	float clear_color[4];
	struct wlr_output_layout *layout;

	struct wl_listener cursor_motion;
	struct wl_listener cursor_motion_absolute;
	struct wl_listener cursor_button;
	struct wl_listener cursor_axis;
};

static void handle_output_frame(struct output_state *output, struct timespec *ts) {
	struct compositor_state *state = output->compositor;
	struct sample_state *sample = state->data;
	struct wlr_output *wlr_output = output->output;

	wlr_output_make_current(wlr_output);

	glClearColor(sample->clear_color[0], sample->clear_color[1],
			sample->clear_color[2], sample->clear_color[3]);
	glClear(GL_COLOR_BUFFER_BIT);

	wlr_output_swap_buffers(wlr_output);
}

static void handle_output_add(struct output_state *ostate) {
	struct sample_state *sample = ostate->compositor->data;
	struct wlr_output *wlr_output = ostate->output;
	struct wlr_xcursor_image *image = sample->xcursor->images[0];

	wlr_output_layout_destroy(sample->layout);
	sample->layout = configure_layout(sample->config, &ostate->compositor->outputs);
	wlr_cursor_attach_output_layout(sample->cursor, sample->layout);

	if (!wlr_output_set_cursor(wlr_output, image->buffer,
			image->width, image->width, image->height)) {
		wlr_log(L_DEBUG, "Failed to set hardware cursor");
		return;
	}
	if (!wlr_output_move_cursor(wlr_output, 0, 0)) {
		wlr_log(L_DEBUG, "Failed to move hardware cursor");
	}
}

static void handle_output_remove(struct output_state *ostate) {
	struct sample_state *sample = ostate->compositor->data;
	wlr_output_layout_destroy(sample->layout);
	sample->layout = configure_layout(sample->config, &ostate->compositor->outputs);
	wlr_cursor_attach_output_layout(sample->cursor, sample->layout);
}

static void handle_output_resolution(struct compositor_state *state,
		struct output_state *ostate) {
	struct sample_state *sample = ostate->compositor->data;
	wlr_output_layout_destroy(sample->layout);
	sample->layout = configure_layout(sample->config, &ostate->compositor->outputs);
	wlr_cursor_attach_output_layout(sample->cursor, sample->layout);
}

static void handle_input_add(struct compositor_state *state, struct
		wlr_input_device *device) {
	struct sample_state *sample = state->data;

	// TODO handle other input devices
	if (device->type == WLR_INPUT_DEVICE_POINTER) {
		wlr_cursor_attach_input_device(sample->cursor, device);
	}
}

static void handle_cursor_motion(struct wl_listener *listener, void *data) {
	struct sample_state *sample = wl_container_of(listener, sample, cursor_motion);
	struct wlr_event_pointer_motion *event = data;
	wlr_cursor_move(sample->cursor, event->delta_x, event->delta_y);
}

static void handle_cursor_motion_absolute(struct wl_listener *listener, void *data) {
	struct sample_state *sample = wl_container_of(listener, sample, cursor_motion_absolute);
	struct wlr_event_pointer_motion_absolute *event = data;

	sample->cur_x = event->x_mm;
	sample->cur_y = event->y_mm;

	struct wlr_xcursor_image *image = sample->xcursor->images[0];

	struct output_state *output;
	wl_list_for_each(output, &sample->compositor->outputs, link) {
		wlr_output_move_cursor(output->output,
				sample->cur_x - image->hotspot_x,
				sample->cur_y - image->hotspot_y);
	}
}

static void handle_cursor_button(struct wl_listener *listener, void *data) {
	struct sample_state *sample = wl_container_of(listener, sample, cursor_button);
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
	struct sample_state *sample = wl_container_of(listener, sample, cursor_axis);
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

int main(int argc, char *argv[]) {
	struct sample_state state = {
		.default_color = { 0.25f, 0.25f, 0.25f, 1 },
		.clear_color = { 0.25f, 0.25f, 0.25f, 1 }
	};

	state.config = parse_args(argc, argv);
	state.cursor = wlr_cursor_init();

	wl_signal_add(&state.cursor->events.motion, &state.cursor_motion);
	state.cursor_motion.notify = handle_cursor_motion;

	wl_signal_add(&state.cursor->events.motion_absolute, &state.cursor_motion_absolute);
	state.cursor_motion_absolute.notify = handle_cursor_motion_absolute;

	wl_signal_add(&state.cursor->events.button, &state.cursor_button);
	state.cursor_button.notify = handle_cursor_button;

	wl_signal_add(&state.cursor->events.axis, &state.cursor_axis);
	state.cursor_axis.notify = handle_cursor_axis;

	struct compositor_state compositor = { 0 };
	compositor.data = &state;
	compositor.output_add_cb = handle_output_add;
	compositor.output_remove_cb = handle_output_remove;
	compositor.output_resolution_cb = handle_output_resolution;
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

	wlr_cursor_set_xcursor(state.cursor, state.xcursor);

	compositor_init(&compositor);
	wl_display_run(compositor.display);
	compositor_fini(&compositor);

	wlr_xcursor_theme_destroy(theme);
	example_config_destroy(state.config);
	wlr_cursor_destroy(state.cursor);
}
