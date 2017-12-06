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
#include <GLES2/gl2.h>
#include <wlr/backend.h>
#include <wlr/backend/session.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_tablet_tool.h>
#include <wlr/types/wlr_tablet_pad.h>
#include <wlr/util/log.h>
#include <math.h>
#include "support/shared.h"
#include "support/cat.h"

struct sample_state {
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
	struct wlr_render *rend = wlr_backend_get_render(wlr_output->backend);

	int32_t width, height;
	wlr_output_effective_resolution(wlr_output, &width, &height);

	wlr_output_make_current(wlr_output);
	wlr_render_bind(rend, wlr_output);
	wlr_render_clear(rend, 0.25, 0.25, 0.25, 1.0);

	float distance = 0.8f * (1 - sample->distance);
	float tool[4] = { distance, distance, distance, 1 };
	for (size_t i = 0; sample->button && i < 4; ++i) {
		tool[i] = sample->tool_color[i];
	}
	float scale = 4;

	float pad_width = sample->width_mm * scale;
	float pad_height = sample->height_mm * scale;
	float pad_x = width / 2.0f - pad_width / 2.0f;
	float pad_y = height / 2.0f - pad_height / 2.0f;

	float *pad = sample->pad_color;

	wlr_render_rect(rend, pad[0], pad[1], pad[2], pad[3],
		pad_x, pad_y, pad_x + pad_width, pad_y + pad_height);

	if (sample->proximity) {
		float x = sample->x_mm * scale - 8 * (sample->pressure + 1) + pad_x;
		float y = sample->y_mm * scale - 8 * (sample->pressure + 1) + pad_y;
		float cir = 16 * (sample->pressure + 1);

		wlr_render_ellipse(rend,
			tool[0], tool[1], tool[2], tool[3],
			x, y, x + cir, y + cir
		);
	}

	wlr_output_swap_buffers(wlr_output);
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
	struct compositor_state compositor = {
		.data = &state,
		.output_frame_cb = handle_output_frame,
		.tool_axis_cb = handle_tool_axis,
		.tool_proximity_cb = handle_tool_proximity,
		.tool_button_cb = handle_tool_button,
		.pad_button_cb = handle_pad_button,
	};
	compositor_init(&compositor);

	if (!wlr_backend_start(compositor.backend)) {
		wlr_log(L_ERROR, "Failed to start backend");
		wlr_backend_destroy(compositor.backend);
		exit(1);
	}
	wl_display_run(compositor.display);

	compositor_fini(&compositor);
}
