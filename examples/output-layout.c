#define _POSIX_C_SOURCE 199309L
#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <limits.h>
#include <wayland-server.h>
#include <wayland-server-protocol.h>
#include <xkbcommon/xkbcommon.h>
#include <GLES2/gl2.h>
#include <wlr/render/matrix.h>
#include <wlr/render/gles2.h>
#include <wlr/render.h>
#include <wlr/util/log.h>
#include <wlr/backend.h>
#include <wlr/backend/session.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_keyboard.h>
#include <math.h>
#include "support/shared.h"
#include "support/config.h"
#include "support/cat.h"

struct sample_state {
	struct example_config *config;
	struct wlr_renderer *renderer;
	struct wlr_texture *cat_texture;
	struct wlr_output_layout *layout;
	float x_offs, y_offs;
	float x_vel, y_vel;
	struct timespec ts_last;
};

static void animate_cat(struct sample_state *sample,
		struct wlr_output *output) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	long ms = (ts.tv_sec - sample->ts_last.tv_sec) * 1000 +
		(ts.tv_nsec - sample->ts_last.tv_nsec) / 1000000;
	// how many seconds have passed since the last time we animated
	float seconds = ms / 1000.0f;

	if (seconds > 0.1f) {
		// XXX when we switch vt, the rendering loop stops so try to detect
		// that and pause when it happens.
		seconds = 0.0f;
	}

	// check for collisions and bounce
	bool ur_collision = !wlr_output_layout_output_at(sample->layout,
			sample->x_offs + 128, sample->y_offs);
	bool ul_collision = !wlr_output_layout_output_at(sample->layout,
			sample->x_offs, sample->y_offs);
	bool ll_collision = !wlr_output_layout_output_at(sample->layout,
			sample->x_offs, sample->y_offs + 128);
	bool lr_collision = !wlr_output_layout_output_at(sample->layout,
			sample->x_offs + 128, sample->y_offs + 128);

	if (ur_collision && ul_collision && ll_collision && lr_collision) {
		// oops we went off the screen somehow
		struct wlr_output_layout_output *l_output =
			wlr_output_layout_get(sample->layout, output);
		sample->x_offs = l_output->x + 20;
		sample->y_offs = l_output->y + 20;
	} else if (ur_collision && ul_collision) {
		sample->y_vel = fabs(sample->y_vel);
	} else if (lr_collision && ll_collision) {
		sample->y_vel = -fabs(sample->y_vel);
	} else if (ll_collision && ul_collision) {
		sample->x_vel = fabs(sample->x_vel);
	} else if (ur_collision && lr_collision) {
		sample->x_vel = -fabs(sample->x_vel);
	} else {
		if (ur_collision || lr_collision) {
			sample->x_vel = -fabs(sample->x_vel);
		}
		if (ul_collision || ll_collision) {
			sample->x_vel = fabs(sample->x_vel);
		}
		if (ul_collision || ur_collision) {
			sample->y_vel = fabs(sample->y_vel);
		}
		if (ll_collision || lr_collision) {
			sample->y_vel = -fabs(sample->y_vel);
		}
	}

	sample->x_offs += sample->x_vel * seconds;
	sample->y_offs += sample->y_vel * seconds;
	sample->ts_last = ts;
}

static void handle_output_frame(struct output_state *output,
		struct timespec *ts) {
	struct compositor_state *state = output->compositor;
	struct sample_state *sample = state->data;
	struct wlr_output *wlr_output = output->output;

	wlr_output_make_current(wlr_output);
	wlr_renderer_begin(sample->renderer, wlr_output);

	animate_cat(sample, output->output);

	struct wlr_box box = {
		.x = sample->x_offs, .y = sample->y_offs,
		.width = 128, .height = 128,
	};
	if (wlr_output_layout_intersects(sample->layout, output->output, &box)) {
		float matrix[16];

		// transform global coordinates to local coordinates
		double local_x = sample->x_offs;
		double local_y = sample->y_offs;
		wlr_output_layout_output_coords(sample->layout, output->output,
			&local_x, &local_y);

		wlr_texture_get_matrix(sample->cat_texture, &matrix,
			&wlr_output->transform_matrix, local_x, local_y);
		wlr_render_with_matrix(sample->renderer,
			sample->cat_texture, &matrix);
	}

	wlr_renderer_end(sample->renderer);
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
}

static void handle_output_remove(struct output_state *ostate) {
	struct sample_state *sample = ostate->compositor->data;
	wlr_output_layout_remove(sample->layout, ostate->output);
}

static void update_velocities(struct compositor_state *state,
		float x_diff, float y_diff) {
	struct sample_state *sample = state->data;
	sample->x_vel += x_diff;
	sample->y_vel += y_diff;
}

static void handle_keyboard_key(struct keyboard_state *kbstate,
		uint32_t keycode, xkb_keysym_t sym, enum wlr_key_state key_state,
		uint64_t time_usec) {
	// NOTE: It may be better to simply refer to our key state during each frame
	// and make this change in pixels/sec^2
	// Also, key repeat
	int delta = 75;
	if (key_state == WLR_KEY_PRESSED) {
		switch (sym) {
		case XKB_KEY_Left:
			update_velocities(kbstate->compositor, -delta, 0);
			break;
		case XKB_KEY_Right:
			update_velocities(kbstate->compositor, delta, 0);
			break;
		case XKB_KEY_Up:
			update_velocities(kbstate->compositor, 0, -delta);
			break;
		case XKB_KEY_Down:
			update_velocities(kbstate->compositor, 0, delta);
			break;
		}
	}
}

int main(int argc, char *argv[]) {
	wlr_log_init(L_DEBUG, NULL);
	struct sample_state state = {0};

	state.x_vel = 500;
	state.y_vel = 500;
	state.layout = wlr_output_layout_create();
	clock_gettime(CLOCK_MONOTONIC, &state.ts_last);

	state.config = parse_args(argc, argv);

	struct compositor_state compositor = { 0 };
	compositor.data = &state;
	compositor.output_add_cb = handle_output_add;
	compositor.output_remove_cb = handle_output_remove;
	compositor.output_frame_cb = handle_output_frame;
	compositor.keyboard_key_cb = handle_keyboard_key;
	compositor_init(&compositor);

	state.renderer = wlr_gles2_renderer_create(compositor.backend);
	state.cat_texture = wlr_render_texture_create(state.renderer);
	wlr_texture_upload_pixels(state.cat_texture, WL_SHM_FORMAT_ABGR8888,
		cat_tex.width, cat_tex.width, cat_tex.height, cat_tex.pixel_data);

	if (!wlr_backend_start(compositor.backend)) {
		wlr_log(L_ERROR, "Failed to start backend");
		wlr_backend_destroy(compositor.backend);
		exit(1);
	}
	wl_display_run(compositor.display);

	wlr_texture_destroy(state.cat_texture);
	wlr_renderer_destroy(state.renderer);
	compositor_fini(&compositor);

	wlr_output_layout_destroy(state.layout);

	example_config_destroy(state.config);
}
