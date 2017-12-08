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
#include <wlr/render/render.h>
#include <wlr/backend.h>
#include <wlr/backend/session.h>
#include <wlr/types/wlr_list.h>
#include <wlr/util/log.h>
#include "support/shared.h"
#include "support/cat.h"

struct sample_state {
	struct wlr_render *rend;
	struct wlr_tex *cat_tex;
	struct wl_list touch_points;
};

struct touch_point {
	int32_t touch_id;
	double x, y;
	struct wl_list link;
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

	struct touch_point *p;
	wl_list_for_each(p, &sample->touch_points, link) {
		int32_t x = p->x * width;
		int32_t y = p->x * width;
		int32_t w = wlr_tex_get_width(sample->cat_tex) / 2;
		int32_t h = wlr_tex_get_height(sample->cat_tex) / 2;
		wlr_render_texture(sample->rend, sample->cat_tex,
			x - w, y - h, x + w, y + h);
	}

	wlr_output_swap_buffers(wlr_output);
}

static void handle_touch_down(struct touch_state *tstate, int32_t touch_id,
		double x, double y, double width, double height) {
	struct sample_state *sample = tstate->compositor->data;
	struct touch_point *point = calloc(1, sizeof(struct touch_point));
	point->touch_id = touch_id;
	point->x = x / width;
	point->y = y / height;
	wl_list_insert(&sample->touch_points, &point->link);
}

static void handle_touch_up(struct touch_state *tstate, int32_t touch_id) {
	struct sample_state *sample = tstate->compositor->data;
	struct touch_point *point, *tmp;
	wl_list_for_each_safe(point, tmp, &sample->touch_points, link) {
		if (point->touch_id == touch_id) {
			wl_list_remove(&point->link);
			break;
		}
	}
}

static void handle_touch_motion(struct touch_state *tstate, int32_t touch_id,
		double x, double y, double width, double height) {
	struct sample_state *sample = tstate->compositor->data;
	struct touch_point *point;
	wl_list_for_each(point, &sample->touch_points, link) {
		if (point->touch_id == touch_id) {
			point->x = x / width;
			point->y = y / height;
			break;
		}
	}
}

int main(int argc, char *argv[]) {
	struct sample_state state;
	wl_list_init(&state.touch_points);

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
