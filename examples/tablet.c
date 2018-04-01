#define _POSIX_C_SOURCE 199309L
#define _XOPEN_SOURCE 500
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
#include <wlr/types/wlr_box.h>
#include <wlr/types/wlr_matrix.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_tablet_pad.h>
#include <wlr/types/wlr_tablet_tool.h>
#include <wlr/util/log.h>
#include <xkbcommon/xkbcommon.h>
#include "support/cat.h"
#include "support/shared.h"

struct sample_state {
	struct wlr_renderer *renderer;
	bool proximity, tap, button;
	double distance;
	double pressure;
	double x, y;
	double x_tilt, y_tilt;
	double width_mm, height_mm;
	double ring;
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

	wlr_output_make_current(wlr_output, NULL);
	wlr_renderer_begin(sample->renderer, wlr_output->width, wlr_output->height);
	wlr_renderer_clear(sample->renderer, (float[]){0.25f, 0.25f, 0.25f, 1});

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
	const struct wlr_box box = {
		.x = left, .y = top,
		.width = pad_width, .height = pad_height,
	};
	wlr_render_rect(sample->renderer, &box, sample->pad_color,
		wlr_output->transform_matrix);

	if (sample->proximity) {
		struct wlr_box box = {
			.x = (sample->x * pad_width) - 8 * (sample->pressure + 1) + left,
			.y = (sample->y * pad_height) - 8 * (sample->pressure + 1) + top,
			.width = 16 * (sample->pressure + 1),
			.height = 16 * (sample->pressure + 1),
		};
		float matrix[9];
		wlr_matrix_project_box(matrix, &box, WL_OUTPUT_TRANSFORM_NORMAL,
			sample->ring, wlr_output->transform_matrix);
		wlr_render_quad_with_matrix(sample->renderer, tool_color, matrix);

		box.x += sample->x_tilt;
		box.y += sample->y_tilt;
		box.width /= 2;
		box.height /= 2;
		wlr_render_rect(sample->renderer, &box, tool_color,
			wlr_output->transform_matrix);
	}

	wlr_renderer_end(sample->renderer);
	wlr_output_swap_buffers(wlr_output, NULL, NULL);
}

static void handle_tool_axis(struct tablet_tool_state *tstate,
			struct wlr_event_tablet_tool_axis *event) {
	struct sample_state *sample = tstate->compositor->data;
	if ((event->updated_axes & WLR_TABLET_TOOL_AXIS_X)) {
		sample->x = event->x;
	}
	if ((event->updated_axes & WLR_TABLET_TOOL_AXIS_Y)) {
		sample->y = event->y;
	}
	if ((event->updated_axes & WLR_TABLET_TOOL_AXIS_DISTANCE)) {
		sample->distance = event->distance;
	}
	if ((event->updated_axes & WLR_TABLET_TOOL_AXIS_PRESSURE)) {
		sample->pressure = event->pressure;
	}
	if ((event->updated_axes & WLR_TABLET_TOOL_AXIS_TILT_X)) {
		sample->x_tilt = event->tilt_x;
	}
	if ((event->updated_axes & WLR_TABLET_TOOL_AXIS_TILT_Y)) {
		sample->y_tilt = event->tilt_y;
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
	float default_color[4] = { 0.5, 0.5, 0.5, 1.0 };
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

static void handle_pad_ring(struct tablet_pad_state *pstate,
		uint32_t ring, double position) {
	struct sample_state *sample = pstate->compositor->data;
	if (position != -1) {
		sample->ring = -(position * (M_PI / 180.0));
	}
}

static void handle_input_add(struct compositor_state *cstate,
		struct wlr_input_device *inputdev) {
	struct sample_state *sample = cstate->data;
	if (inputdev->type == WLR_INPUT_DEVICE_TABLET_TOOL) {
		sample->width_mm = inputdev->width_mm == 0 ?
			20 : inputdev->width_mm;
		sample->height_mm = inputdev->height_mm == 0 ?
			10 : inputdev->height_mm;
	}
}

int main(int argc, char *argv[]) {
	wlr_log_init(L_DEBUG, NULL);
	struct sample_state state = {
		.tool_color = { 1, 1, 1, 1 },
		.pad_color = { 0.5, 0.5, 0.5, 1.0 }
	};
	struct compositor_state compositor = {
		.data = &state,
		.output_frame_cb = handle_output_frame,
		.tool_axis_cb = handle_tool_axis,
		.tool_proximity_cb = handle_tool_proximity,
		.tool_button_cb = handle_tool_button,
		.pad_button_cb = handle_pad_button,
		.pad_ring_cb = handle_pad_ring,
		.input_add_cb = handle_input_add,
		0
	};
	compositor_init(&compositor);

	struct wlr_egl *egl = wlr_backend_get_egl(compositor.backend);
	state.renderer = wlr_gles2_renderer_create(egl);
	if (!state.renderer) {
		wlr_log(L_ERROR, "Could not start compositor, OOM");
		exit(EXIT_FAILURE);
	}
	if (!wlr_backend_start(compositor.backend)) {
		wlr_log(L_ERROR, "Failed to start backend");
		wlr_backend_destroy(compositor.backend);
		exit(1);
	}
	wl_display_run(compositor.display);

	wlr_renderer_destroy(state.renderer);
	compositor_fini(&compositor);
}
