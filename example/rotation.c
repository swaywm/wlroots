#define _POSIX_C_SOURCE 199309L
#define _XOPEN_SOURCE 500
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <strings.h>
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
#include <wlr/types/wlr_keyboard.h>
#include <math.h>
#include "shared.h"
#include "cat.h"

struct sample_state {
	struct wl_list config;
	struct wlr_renderer *renderer;
	struct wlr_surface *cat_texture;
};

struct output_data {
	float x_offs, y_offs;
	float x_vel, y_vel;
};

struct output_config {
	char *name;
	enum wl_output_transform transform;
	struct wl_list link;
};

static void handle_output_frame(struct output_state *output, struct timespec *ts) {
	struct compositor_state *state = output->compositor;
	struct sample_state *sample = state->data;
	struct output_data *odata = output->data;
	struct wlr_output *wlr_output = output->output;

	int32_t width, height;
	wlr_output_effective_resolution(wlr_output, &width, &height);

	wlr_renderer_begin(sample->renderer, wlr_output);

	float matrix[16];
	for (int y = -128 + (int)odata->y_offs; y < height; y += 128) {
		for (int x = -128 + (int)odata->x_offs; x < width; x += 128) {
			wlr_surface_get_matrix(sample->cat_texture, &matrix,
				&wlr_output->transform_matrix, x, y);
			wlr_render_with_matrix(sample->renderer,
					sample->cat_texture, &matrix);
		}
	}

	wlr_renderer_end(sample->renderer);

	long ms = (ts->tv_sec - output->last_frame.tv_sec) * 1000 +
		(ts->tv_nsec - output->last_frame.tv_nsec) / 1000000;
	float seconds = ms / 1000.0f;

	odata->x_offs += odata->x_vel * seconds;
	odata->y_offs += odata->y_vel * seconds;
	if (odata->x_offs > 128) odata->x_offs = 0;
	if (odata->y_offs > 128) odata->y_offs = 0;
}

static void handle_output_add(struct output_state *output) {
	struct output_data *odata = calloc(1, sizeof(struct output_data));
	odata->x_offs = odata->y_offs = 0;
	odata->x_vel = odata->y_vel = 128;
	output->data = odata;
	struct sample_state *state = output->compositor->data;

	struct output_config *conf;
	wl_list_for_each(conf, &state->config, link) {
		if (strcmp(conf->name, output->output->name) == 0) {
			wlr_output_transform(output->output, conf->transform);
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
		xkb_keysym_t sym, enum wlr_key_state key_state) {
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

static void usage(const char *name, int ret) {
	fprintf(stderr,
		"usage: %s [-d <name> [-r <rotation> | -f]]*\n"
		"\n"
		" -o <output>    The name of the DRM display. e.g. DVI-I-1.\n"
		" -r <rotation>  The rotation counter clockwise. Valid values are 90, 180, 270.\n"
		" -f             Flip the output along the vertical axis.\n", name);

	exit(ret);
}

static void parse_args(int argc, char *argv[], struct wl_list *config) {
	struct output_config *oc = NULL;

	int c;
	while ((c = getopt(argc, argv, "o:r:fh")) != -1) {
		switch (c) {
		case 'o':
			oc = calloc(1, sizeof(*oc));
			oc->name = optarg;
			oc->transform = WL_OUTPUT_TRANSFORM_NORMAL;
			wl_list_insert(config, &oc->link);
			break;
		case 'r':
			if (!oc) {
				fprintf(stderr, "You must specify an output first\n");
				usage(argv[0], 1);
			}

			if (oc->transform != WL_OUTPUT_TRANSFORM_NORMAL
					&& oc->transform != WL_OUTPUT_TRANSFORM_FLIPPED) {
				fprintf(stderr, "Rotation for %s already specified\n", oc->name);
				usage(argv[0], 1);
			}

			if (strcmp(optarg, "90") == 0) {
				oc->transform += WL_OUTPUT_TRANSFORM_90;
			} else if (strcmp(optarg, "180") == 0) {
				oc->transform += WL_OUTPUT_TRANSFORM_180;
			} else if (strcmp(optarg, "270") == 0) {
				oc->transform += WL_OUTPUT_TRANSFORM_270;
			} else {
				fprintf(stderr, "Invalid rotation '%s'\n", optarg);
				usage(argv[0], 1);
			}
			break;
		case 'f':
			if (!oc) {
				fprintf(stderr, "You must specify an output first\n");
				usage(argv[0], 1);
			}

			if (oc->transform >= WL_OUTPUT_TRANSFORM_FLIPPED) {
				fprintf(stderr, "Flip for %s already specified\n", oc->name);
				usage(argv[0], 1);
			}

			oc->transform += WL_OUTPUT_TRANSFORM_FLIPPED;
			break;
		case 'h':
		case '?':
			usage(argv[0], c != 'h');
		}
	}
}

int main(int argc, char *argv[]) {
	struct sample_state state = { 0 };
	wl_list_init(&state.config);
	parse_args(argc, argv, &state.config);

	struct compositor_state compositor = { 0 };
	compositor.data = &state;
	compositor.output_add_cb = handle_output_add;
	compositor.output_remove_cb = handle_output_remove;
	compositor.output_frame_cb = handle_output_frame;
	compositor.keyboard_key_cb = handle_keyboard_key;
	compositor_init(&compositor);

	state.renderer = wlr_gles3_renderer_init();
	state.cat_texture = wlr_render_surface_init(state.renderer);
	wlr_surface_attach_pixels(state.cat_texture, GL_RGBA,
		cat_tex.width, cat_tex.height, cat_tex.pixel_data);

	compositor_run(&compositor);

	wlr_surface_destroy(state.cat_texture);
	wlr_renderer_destroy(state.renderer);

	struct output_config *ptr, *tmp;
	wl_list_for_each_safe(ptr, tmp, &state.config, link) {
		free(ptr);
	}
}
