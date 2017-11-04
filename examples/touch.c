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
#include <GLES2/gl2.h>
#include <wlr/render/matrix.h>
#include <wlr/render/gles2.h>
#include "wlr/render.h"
#include <wlr/backend.h>
#include <wlr/backend/session.h>
#include <wlr/types/wlr_list.h>
#include <wlr/util/log.h>
#include "support/shared.h"
#include "support/cat.h"

struct sample_state {
	struct wlr_render *rend;
	struct wlr_tex *cat_tex;
	struct wlr_list *touch_points;
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

	wlr_output_make_current(wlr_output);
	wlr_render_bind(sample->rend, wlr_output);
	wlr_render_clear(sample->rend, 0.25, 0.25, 0.25, 1.0);

	for (size_t i = 0; i < sample->touch_points->length; ++i) {
		struct touch_point *p = sample->touch_points->items[i];
		int32_t x = p->x * width;
		int32_t y = p->x * width;
		int32_t w = sample->cat_tex->width / 2;
		int32_t h = sample->cat_tex->height / 2;
		wlr_render_texture(sample->rend, sample->cat_tex,
			x - w, y - h, x + w, y + h, 0);
	}

	wlr_output_swap_buffers(wlr_output);
}

static void handle_touch_down(struct touch_state *tstate, int32_t slot,
		double x, double y, double width, double height) {
	struct sample_state *sample = tstate->compositor->data;
	struct touch_point *point = calloc(1, sizeof(struct touch_point));
	point->slot = slot;
	point->x = x / width;
	point->y = y / height;
	if (wlr_list_add(sample->touch_points, point) == -1) {
		free(point);
	}
}

static void handle_touch_up(struct touch_state *tstate, int32_t slot) {
	struct sample_state *sample = tstate->compositor->data;
	for (size_t i = 0; i < sample->touch_points->length; ++i) {
		struct touch_point *point = sample->touch_points->items[i];
		if (point->slot == slot) {
			wlr_list_del(sample->touch_points, i);
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
		.touch_points = wlr_list_create()
	};
	struct compositor_state compositor = { 0,
		.data = &state,
		.output_frame_cb = handle_output_frame,
		.touch_down_cb = handle_touch_down,
		.touch_up_cb = handle_touch_up,
		.touch_motion_cb = handle_touch_motion,
	};
	compositor_init(&compositor);

	state.rend = wlr_backend_get_render(compositor.backend);

	state.cat_tex = wlr_tex_from_pixels(state.rend, WL_SHM_FORMAT_ARGB8888,
		cat_tex.width * 4, cat_tex.width, cat_tex.height, cat_tex.pixel_data);
	if (!state.cat_tex) {
		wlr_log(L_ERROR, "Could not start compositor, OOM");
		exit(EXIT_FAILURE);
	}

	if (!wlr_backend_start(compositor.backend)) {
		wlr_log(L_ERROR, "Failed to start backend");
		wlr_backend_destroy(compositor.backend);
		exit(1);
	}
	wl_display_run(compositor.display);

	wlr_tex_destroy(state.cat_tex);
	compositor_fini(&compositor);
}
