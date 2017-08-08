#define _POSIX_C_SOURCE 199309L
#define _XOPEN_SOURCE 500
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <unistd.h>
#include <math.h>
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
#include <wlr/xcursor.h>
#include <wlr/util/log.h>
#include "shared.h"
#include "cat.h"

struct sample_state {
	struct wlr_cursor *cursor;
	double cur_x, cur_y;
	float default_color[4];
	float clear_color[4];
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

static void handle_pointer_motion(struct pointer_state *pstate,
		double d_x, double d_y) {
	struct sample_state *state = pstate->compositor->data;
	state->cur_x += d_x;
	state->cur_y += d_y;

	struct wlr_cursor_image *image = state->cursor->images[0];

	struct output_state *output;
	wl_list_for_each(output, &pstate->compositor->outputs, link) {
		wlr_output_move_cursor(output->output,
				state->cur_x - image->hotspot_x,
				state->cur_y - image->hotspot_y);
	}
}

static void handle_pointer_motion_absolute(struct pointer_state *pstate,
		double x, double y) {
	struct sample_state *state = pstate->compositor->data;
	state->cur_x = x;
	state->cur_y = y;

	struct wlr_cursor_image *image = state->cursor->images[0];

	struct output_state *output;
	wl_list_for_each(output, &pstate->compositor->outputs, link) {
		wlr_output_move_cursor(output->output,
				state->cur_x - image->hotspot_x,
				state->cur_y - image->hotspot_y);
	}
}

static void handle_pointer_button(struct pointer_state *pstate,
		uint32_t button, enum wlr_button_state state) {
	struct sample_state *sample = pstate->compositor->data;
	float (*color)[4];
	if (state == WLR_BUTTON_RELEASED) {
		color = &sample->default_color;
	} else {
		float red[4] = { 0.25f, 0.25f, 0.25f, 1 };
		red[button % 3] = 1;
		color = &red;
	}
	memcpy(&sample->clear_color, color, sizeof(*color));
}

static void handle_pointer_axis(struct pointer_state *pstate,
	enum wlr_axis_source source,
	enum wlr_axis_orientation orientation,
	double delta) {
	struct sample_state *sample = pstate->compositor->data;
	for (size_t i = 0; i < 3; ++i) {
		sample->default_color[i] += delta > 0 ? -0.05f : 0.05f;
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

static void handle_output_add(struct output_state *ostate) {
	struct sample_state *state = ostate->compositor->data;
	struct wlr_output *wlr_output = ostate->output;
	struct wlr_cursor_image *image = state->cursor->images[0];
	if (!wlr_output_set_cursor(wlr_output, image->buffer,
			image->width, image->width, image->height)) {
		wlr_log(L_DEBUG, "Failed to set hardware cursor");
		return;
	}
	if (!wlr_output_move_cursor(wlr_output, 0, 0)) {
		wlr_log(L_DEBUG, "Failed to move hardware cursor");
	}
}

int main(int argc, char *argv[]) {
	struct sample_state state = {
		.default_color = { 0.25f, 0.25f, 0.25f, 1 },
		.clear_color = { 0.25f, 0.25f, 0.25f, 1 }
	};
	struct compositor_state compositor = { 0 };
	compositor.data = &state;
	compositor.output_add_cb = handle_output_add;
	compositor.output_frame_cb = handle_output_frame;
	compositor.pointer_motion_cb = handle_pointer_motion;
	compositor.pointer_motion_absolute_cb = handle_pointer_motion_absolute;
	compositor.pointer_button_cb = handle_pointer_button;
	compositor.pointer_axis_cb = handle_pointer_axis;

	struct wlr_cursor_theme *theme = wlr_cursor_theme_load("default", 16);
	if (!theme) {
		wlr_log(L_ERROR, "Failed to load cursor theme");
		return 1;
	}
	state.cursor = wlr_cursor_theme_get_cursor(theme, "left_ptr");
	if (!state.cursor) {
		wlr_log(L_ERROR, "Failed to load left_ptr cursor");
		return 1;
	}

	compositor_init(&compositor);
	compositor_run(&compositor);

	wlr_cursor_theme_destroy(theme);
}
