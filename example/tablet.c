#define _POSIX_C_SOURCE 199309L
#define _XOPEN_SOURCE 500
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <unistd.h>
#include <wayland-server.h>
#include <wayland-server-protocol.h>
#include <xkbcommon/xkbcommon.h>
#include <GLES3/gl3.h>
#include <wlr/render/matrix.h>
#include <wlr/render/gles3.h>
#include <wlr/render.h>
#include <wlr/backend.h>
#include <wlr/session.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_tablet_tool.h>
#include <wlr/types/wlr_tablet_pad.h>
#include <math.h>
#include "shared.h"
#include "cat.h"

struct sample_state {
	struct wlr_renderer *renderer;
	bool proximity, tap, button;
	double distance;
	double pressure;
	double x_mm, y_mm;
	double width_mm, height_mm;
	struct wl_list link;
	float tool_color[4];
	float pad_color[4];
};

static void handle_output_frame(struct output_state *output, struct timespec *ts) {
	struct compositor_state *state = output->compositor;
	struct sample_state *sample = state->data;
	struct wlr_output *wlr_output = output->output;

	int32_t width, height;
	wlr_output_effective_resolution(wlr_output, &width, &height);

	wlr_renderer_begin(sample->renderer, wlr_output);

	float matrix[16], view[16];
	float distance = 0.8f * (1 - sample->distance);
	float tool_color[4] = { distance, distance, distance, 1 };
	for (size_t i = 0; sample->button && i < 4; ++i) {
		tool_color[i] = sample->tool_color[i];
	}
	float scale = 4;

	float pad_width = sample->width_mm * scale;
	float pad_height = sample->height_mm * scale;
	float left = width / 2.0f - pad_width / 2.0f;
	float top = height / 2.0f - pad_height / 2.0f;
	wlr_matrix_translate(&matrix, left, top, 0);
	wlr_matrix_scale(&view, pad_width, pad_height, 1);
	wlr_matrix_mul(&matrix, &view, &view);
	wlr_matrix_mul(&wlr_output->transform_matrix, &view, &matrix);
	wlr_render_colored_quad(sample->renderer, &sample->pad_color, &matrix);

	if (sample->proximity) {
		wlr_matrix_translate(&matrix,
				sample->x_mm * scale - 8 * (sample->pressure + 1) + left,
				sample->y_mm * scale - 8 * (sample->pressure + 1) + top, 0);
		wlr_matrix_scale(&view,
				16 * (sample->pressure + 1),
				16 * (sample->pressure + 1), 1);
		wlr_matrix_mul(&matrix, &view, &view);
		wlr_matrix_mul(&wlr_output->transform_matrix, &view, &matrix);
		wlr_render_colored_ellipse(sample->renderer, &tool_color, &matrix);
	}

	wlr_renderer_end(sample->renderer);
}

static void handle_tool_axis(struct tablet_tool_state *tstate,
			struct wlr_event_tablet_tool_axis *event) {
	struct sample_state *sample = tstate->compositor->data;
	sample->width_mm = event->width_mm;
	sample->height_mm = event->height_mm;
	if ((event->updated_axes & WLR_TABLET_TOOL_AXIS_X)) {
		sample->x_mm = event->x_mm;
	}
	if ((event->updated_axes & WLR_TABLET_TOOL_AXIS_Y)) {
		sample->y_mm = event->y_mm;
	}
	if ((event->updated_axes & WLR_TABLET_TOOL_AXIS_DISTANCE)) {
		sample->distance = event->distance;
	}
	if ((event->updated_axes & WLR_TABLET_TOOL_AXIS_PRESSURE)) {
		sample->pressure = event->pressure;
	}
}

static void handle_tool_proximity(struct tablet_tool_state *tstate,
		enum wlr_tablet_tool_proximity_state state) {
	struct sample_state *sample = tstate->compositor->data;
	sample->proximity = state == WLR_TABLET_TOOL_PROXIMITY_IN;
}

static void handle_tool_button(struct tablet_tool_state *tstate,
		uint32_t button, enum wlr_button_state state) {
	struct sample_state *sample = tstate->compositor->data;
	if (state == WLR_BUTTON_RELEASED) {
		sample->button = false;
	} else {
		sample->button = true;
		for (size_t i = 0; i < 3; ++i) {
			if (button % 3 == i) {
				sample->tool_color[i] = 0;
			} else {
				sample->tool_color[i] = 1;
			}
		}
	}
}

static void handle_pad_button(struct tablet_pad_state *pstate,
		uint32_t button, enum wlr_button_state state) {
	struct sample_state *sample = pstate->compositor->data;
	float default_color[4] = { 0.75, 0.75, 0.75, 1.0 };
	if (state == WLR_BUTTON_RELEASED) {
		memcpy(sample->pad_color, default_color, sizeof(default_color));
	} else {
		for (size_t i = 0; i < 3; ++i) {
			if (button % 3 == i) {
				sample->pad_color[i] = 0;
			} else {
				sample->pad_color[i] = 1;
			}
		}
	}
}

int main(int argc, char *argv[]) {
	struct sample_state state = {
		.tool_color = { 1, 1, 1, 1 },
		.pad_color = { 0.75, 0.75, 0.75, 1.0 }
	};
	struct compositor_state compositor = { 0,
		.data = &state,
		.output_frame_cb = handle_output_frame,
		.tool_axis_cb = handle_tool_axis,
		.tool_proximity_cb = handle_tool_proximity,
		.tool_button_cb = handle_tool_button,
		.pad_button_cb = handle_pad_button,
	};
	compositor_init(&compositor);

	state.renderer = wlr_gles3_renderer_init();

	compositor_run(&compositor);

	wlr_renderer_destroy(state.renderer);
}
