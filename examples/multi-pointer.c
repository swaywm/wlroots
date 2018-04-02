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

struct sample_state;

struct sample_cursor {
	struct sample_state *state;
	struct wlr_input_device *device;
	struct wlr_cursor *cursor;
	struct wl_list link;

	struct wl_listener cursor_motion;
	struct wl_listener cursor_motion_absolute;
	struct wl_listener cursor_button;
	struct wl_listener cursor_axis;
};

struct sample_state {
	struct compositor_state *compositor;
	struct example_config *config;
	struct wlr_xcursor *xcursor;
	float default_color[4];
	float clear_color[4];
	struct wlr_output_layout *layout;
	struct wl_list cursors; // sample_cursor::link
};

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

	struct sample_cursor *cursor;
	wl_list_for_each(cursor, &sample->cursors, link) {
		example_config_configure_cursor(sample->config, cursor->cursor,
			sample->compositor);

		struct wlr_xcursor_image *image = sample->xcursor->images[0];
		wlr_cursor_set_image(cursor->cursor, image->buffer, image->width,
			image->width, image->height, image->hotspot_x, image->hotspot_y, 0);

		wlr_cursor_warp(cursor->cursor, NULL, cursor->cursor->x,
			cursor->cursor->y);
	}
}

static void handle_output_remove(struct output_state *ostate) {
	struct sample_state *sample = ostate->compositor->data;

	wlr_output_layout_remove(sample->layout, ostate->output);

	struct sample_cursor *cursor;
	wl_list_for_each(cursor, &sample->cursors, link) {
		example_config_configure_cursor(sample->config, cursor->cursor,
			sample->compositor);
	}
}

static void handle_cursor_motion(struct wl_listener *listener, void *data) {
	struct sample_cursor *cursor =
		wl_container_of(listener, cursor, cursor_motion);
	struct wlr_event_pointer_motion *event = data;
	wlr_cursor_move(cursor->cursor, event->device, event->delta_x,
		event->delta_y);
}

static void handle_cursor_motion_absolute(struct wl_listener *listener,
		void *data) {
	struct sample_cursor *cursor =
		wl_container_of(listener, cursor, cursor_motion_absolute);
	struct wlr_event_pointer_motion_absolute *event = data;
	wlr_cursor_warp_absolute(cursor->cursor, event->device, event->x, event->y);
}

static void handle_input_add(struct compositor_state *state,
		struct wlr_input_device *device) {
	struct sample_state *sample = state->data;

	if (device->type != WLR_INPUT_DEVICE_POINTER) {
		return;
	}

	struct sample_cursor *cursor = calloc(1, sizeof(struct sample_cursor));
	cursor->state = sample;
	cursor->device = device;

	cursor->cursor = wlr_cursor_create();
	wlr_cursor_attach_output_layout(cursor->cursor, sample->layout);
	wlr_cursor_map_to_region(cursor->cursor, sample->config->cursor.mapped_box);

	wl_signal_add(&cursor->cursor->events.motion, &cursor->cursor_motion);
	cursor->cursor_motion.notify = handle_cursor_motion;
	wl_signal_add(&cursor->cursor->events.motion_absolute,
		&cursor->cursor_motion_absolute);
	cursor->cursor_motion_absolute.notify = handle_cursor_motion_absolute;

	wlr_cursor_attach_input_device(cursor->cursor, device);
	example_config_configure_cursor(sample->config, cursor->cursor,
		sample->compositor);

	struct wlr_xcursor_image *image = sample->xcursor->images[0];
	wlr_cursor_set_image(cursor->cursor, image->buffer, image->width * 4,
		image->width, image->height, image->hotspot_x, image->hotspot_y, 0);

	wl_list_insert(&sample->cursors, &cursor->link);
}

static void cursor_destroy(struct sample_cursor *cursor) {
	wl_list_remove(&cursor->link);
	wl_list_remove(&cursor->cursor_motion.link);
	wl_list_remove(&cursor->cursor_motion_absolute.link);
	wlr_cursor_destroy(cursor->cursor);
	free(cursor);
}

static void handle_input_remove(struct compositor_state *state,
		struct wlr_input_device *device) {
	struct sample_state *sample = state->data;
	struct sample_cursor *cursor;
	wl_list_for_each(cursor, &sample->cursors, link) {
		if (cursor->device == device) {
			cursor_destroy(cursor);
			break;
		}
	}
}

int main(int argc, char *argv[]) {
	wlr_log_init(L_DEBUG, NULL);
	struct sample_state state = {
		.default_color = { 0.25f, 0.25f, 0.25f, 1 },
		.clear_color = { 0.25f, 0.25f, 0.25f, 1 },
	};

	wl_list_init(&state.cursors);

	state.config = parse_args(argc, argv);
	state.layout = wlr_output_layout_create();

	struct compositor_state compositor = { 0 };
	compositor.data = &state;
	compositor.output_add_cb = handle_output_add;
	compositor.output_remove_cb = handle_output_remove;
	compositor.output_frame_cb = handle_output_frame;
	compositor.input_add_cb = handle_input_add;
	compositor.input_remove_cb = handle_input_remove;

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

	compositor_init(&compositor);
	if (!wlr_backend_start(compositor.backend)) {
		wlr_log(L_ERROR, "Failed to start backend");
		wlr_backend_destroy(compositor.backend);
		exit(1);
	}
	wl_display_run(compositor.display);
	compositor_fini(&compositor);

	struct sample_cursor *cursor, *tmp_cursor;
	wl_list_for_each_safe(cursor, tmp_cursor, &state.cursors, link) {
		cursor_destroy(cursor);
	}

	wlr_xcursor_theme_destroy(theme);
	example_config_destroy(state.config);
	wlr_output_layout_destroy(state.layout);
}
