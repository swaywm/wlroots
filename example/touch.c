#define _POSIX_C_SOURCE 199309L
#define _XOPEN_SOURCE 500
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
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
#include <wlr/util/list.h>
#include "shared.h"
#include "cat.h"

struct sample_state {
	struct wlr_renderer *renderer;
	struct wlr_surface *cat_texture;
	list_t *touch_points;
};

struct touch_point {
	int32_t slot;
	double x, y;
};

static void handle_output_frame(struct output_state *output, struct timespec *ts) {
	struct compositor_state *state = output->compositor;
	struct sample_state *sample = state->data;
	struct wlr_output *wlr_output = output->output;

	int32_t width, height;
	wlr_output_effective_resolution(wlr_output, &width, &height);
	wlr_renderer_begin(sample->renderer, wlr_output);

	float matrix[16];
	for (size_t i = 0; i < sample->touch_points->length; ++i) {
		struct touch_point *p = sample->touch_points->items[i];
		wlr_surface_get_matrix(sample->cat_texture, &matrix,
			&wlr_output->transform_matrix,
			(int)(p->x * width) - sample->cat_texture->width / 2,
			(int)(p->y * height) - sample->cat_texture->height / 2);
		wlr_render_with_matrix(sample->renderer,
				sample->cat_texture, &matrix);
	}

	wlr_renderer_end(sample->renderer);
}

static void handle_touch_down(struct touch_state *tstate, int32_t slot,
		double x, double y, double width, double height) {
	struct sample_state *sample = tstate->compositor->data;
	struct touch_point *point = calloc(1, sizeof(struct touch_state));
	point->slot = slot;
	point->x = x / width;
	point->y = y / height;
	list_add(sample->touch_points, point);
}

static void handle_touch_up(struct touch_state *tstate, int32_t slot) {
	struct sample_state *sample = tstate->compositor->data;
	for (size_t i = 0; i < sample->touch_points->length; ++i) {
		struct touch_point *point = sample->touch_points->items[i];
		if (point->slot == slot) {
			list_del(sample->touch_points, i);
			break;
		}
	}
}

static void handle_touch_motion(struct touch_state *tstate, int32_t slot,
		double x, double y, double width, double height) {
	struct sample_state *sample = tstate->compositor->data;
	for (size_t i = 0; i < sample->touch_points->length; ++i) {
		struct touch_point *point = sample->touch_points->items[i];
		if (point->slot == slot) {
			point->x = x / width;
			point->y = y / height;
			break;
		}
	}
}

int main(int argc, char *argv[]) {
	struct sample_state state = {
		.touch_points = list_create()
	};
	struct compositor_state compositor = { 0,
		.data = &state,
		.output_frame_cb = handle_output_frame,
		.touch_down_cb = handle_touch_down,
		.touch_up_cb = handle_touch_up,
		.touch_motion_cb = handle_touch_motion,
	};
	compositor_init(&compositor);

	state.renderer = wlr_gles3_renderer_init();
	state.cat_texture = wlr_render_surface_init(state.renderer);
	wlr_surface_attach_pixels(state.cat_texture, GL_RGBA,
		cat_tex.width, cat_tex.height, cat_tex.pixel_data);

	compositor_run(&compositor);

	wlr_surface_destroy(state.cat_texture);
	wlr_renderer_destroy(state.renderer);
}
