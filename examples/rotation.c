#define _POSIX_C_SOURCE 199309L
#define _XOPEN_SOURCE 500
#include <GLES2/gl2.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>
#include <wayland-server-protocol.h>
#include <wayland-server.h>
#include <wlr/backend.h>
#include <wlr/backend/session.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/render/gles2.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_matrix.h>
#include <wlr/util/log.h>
#include <xkbcommon/xkbcommon.h>
#include "support/shared.h"
#include "support/config.h"
#include "support/cat.h"

struct sample_state {
	struct example_config *config;
	struct wlr_renderer *renderer;
	struct wlr_texture *cat_texture;
};

struct output_data {
	float x_offs, y_offs;
	float x_vel, y_vel;
};

static void handle_output_frame(struct output_state *output, struct timespec *ts) {
	struct compositor_state *state = output->compositor;
	struct sample_state *sample = state->data;
	struct output_data *odata = output->data;
	struct wlr_output *wlr_output = output->output;

	int32_t width, height;
	wlr_output_effective_resolution(wlr_output, &width, &height);

	wlr_output_make_current(wlr_output, NULL);
	wlr_renderer_begin(sample->renderer, wlr_output->width, wlr_output->height);
	wlr_renderer_clear(sample->renderer, (float[]){0.25f, 0.25f, 0.25f, 1});

	for (int y = -128 + (int)odata->y_offs; y < height; y += 128) {
		for (int x = -128 + (int)odata->x_offs; x < width; x += 128) {
			wlr_render_texture(sample->renderer, sample->cat_texture,
				wlr_output->transform_matrix, x, y, 1.0f);
		}
	}

	wlr_renderer_end(sample->renderer);
	wlr_output_swap_buffers(wlr_output, NULL, NULL);

	long ms = (ts->tv_sec - output->last_frame.tv_sec) * 1000 +
		(ts->tv_nsec - output->last_frame.tv_nsec) / 1000000;
	float seconds = ms / 1000.0f;

	odata->x_offs += odata->x_vel * seconds;
	odata->y_offs += odata->y_vel * seconds;
	if (odata->x_offs > 128) {
		odata->x_offs = 0;
	}
	if (odata->y_offs > 128) {
		odata->y_offs = 0;
	}
}

static void handle_output_add(struct output_state *output) {
	struct output_data *odata = calloc(1, sizeof(struct output_data));
	odata->x_offs = odata->y_offs = 0;
	odata->x_vel = odata->y_vel = 128;
	output->data = odata;
	struct sample_state *state = output->compositor->data;

	struct output_config *conf;
	wl_list_for_each(conf, &state->config->outputs, link) {
		if (strcmp(conf->name, output->output->name) == 0) {
			wlr_output_set_transform(output->output, conf->transform);
			break;
		}
	}
}

static void handle_output_remove(struct output_state *output) {
	free(output->data);
}

static void update_velocities(struct compositor_state *state,
		float x_diff, float y_diff) {
	struct output_state *output;
	wl_list_for_each(output, &state->outputs, link) {
		struct output_data *odata = output->data;
		odata->x_vel += x_diff;
		odata->y_vel += y_diff;
	}
}

static void handle_keyboard_key(struct keyboard_state *kbstate,
		uint32_t keycode, xkb_keysym_t sym, enum wlr_key_state key_state,
		uint64_t time_usec) {
	// NOTE: It may be better to simply refer to our key state during each frame
	// and make this change in pixels/sec^2
	// Also, key repeat
	if (key_state == WLR_KEY_PRESSED) {
		switch (sym) {
		case XKB_KEY_Left:
			update_velocities(kbstate->compositor, -16, 0);
			break;
		case XKB_KEY_Right:
			update_velocities(kbstate->compositor, 16, 0);
			break;
		case XKB_KEY_Up:
			update_velocities(kbstate->compositor, 0, -16);
			break;
		case XKB_KEY_Down:
			update_velocities(kbstate->compositor, 0, 16);
			break;
		}
	}
}

int main(int argc, char *argv[]) {
	wlr_log_init(L_DEBUG, NULL);
	struct sample_state state = {0};
	state.config = parse_args(argc, argv);

	struct compositor_state compositor = { 0 };
	compositor.data = &state;
	compositor.output_add_cb = handle_output_add;
	compositor.output_remove_cb = handle_output_remove;
	compositor.output_frame_cb = handle_output_frame;
	compositor.keyboard_key_cb = handle_keyboard_key;
	compositor_init(&compositor);

	state.renderer = wlr_gles2_renderer_create(compositor.backend);
	if (!state.renderer) {
		wlr_log(L_ERROR, "Could not start compositor, OOM");
		exit(EXIT_FAILURE);
	}
	state.cat_texture = wlr_render_texture_create(state.renderer);
	if (!state.cat_texture) {
		wlr_log(L_ERROR, "Could not start compositor, OOM");
		exit(EXIT_FAILURE);
	}
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

	example_config_destroy(state.config);
}
