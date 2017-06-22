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
#include <GLES3/gl3.h>
#include <wlr/render/matrix.h>
#include <wlr/render/gles3.h>
#include <wlr/render.h>
#include <wlr/backend.h>
#include <wlr/session.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/util/log.h>
#include "shared.h"
#include "cat.h"

struct sample_state {
	struct wlr_renderer *renderer;
	struct wlr_surface *cat_texture;
	int cur_x, cur_y;
	float default_color[4];
	float clear_color[4];
};

static void handle_output_frame(struct output_state *output, struct timespec *ts) {
	struct compositor_state *state = output->compositor;
	struct sample_state *sample = state->data;
	struct wlr_output *wlr_output = output->output;

	wlr_renderer_begin(sample->renderer, wlr_output);
	glClearColor(sample->clear_color[0], sample->clear_color[1],
			sample->clear_color[2], sample->clear_color[3]);
	glClear(GL_COLOR_BUFFER_BIT);

	float matrix[16];
	wlr_surface_get_matrix(sample->cat_texture, &matrix,
		&wlr_output->transform_matrix, sample->cur_x, sample->cur_y);
	wlr_render_with_matrix(sample->renderer,
			sample->cat_texture, &matrix);

	wlr_renderer_end(sample->renderer);
}

static void handle_pointer_motion(struct pointer_state *pstate,
		double d_x, double d_y) {
	struct sample_state *state = pstate->compositor->data;
	state->cur_x += d_x;
	state->cur_y += d_y;
}

static void handle_pointer_motion_absolute(struct pointer_state *pstate,
		double x, double y) {
	struct sample_state *state = pstate->compositor->data;
	state->cur_x = x;
	state->cur_y = y;
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
	struct wlr_output *wlr_output = ostate->output;
	int width = 16, height = 16;
	if (!wlr_output_set_cursor(wlr_output, cat_tex.pixel_data,
			width * 4, width, height)) {
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
	compositor_init(&compositor);

	state.renderer = wlr_gles3_renderer_init();
	state.cat_texture = wlr_render_surface_init(state.renderer);
	wlr_surface_attach_pixels(state.cat_texture, GL_RGBA,
		cat_tex.width, cat_tex.height, cat_tex.pixel_data);

	compositor_run(&compositor);

	wlr_surface_destroy(state.cat_texture);
	wlr_renderer_destroy(state.renderer);
}
