#define _POSIX_C_SOURCE 199309L
#define _XOPEN_SOURCE 500
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
#include "shared.h"
#include "cat.h"

struct sample_state {
	struct wl_list config;
	struct wlr_renderer *renderer;
	struct wlr_texture *cat_texture;
	struct wlr_output_layout *layout;
	float x_offs, y_offs;
	float x_vel, y_vel;
	struct wlr_output *main_output;
	struct wl_list outputs;
};

struct output_config {
	char *name;
	enum wl_output_transform transform;
	int x, y;
	struct wl_list link;
};

static void handle_output_frame(struct output_state *output, struct timespec *ts) {
	struct compositor_state *state = output->compositor;
	struct sample_state *sample = state->data;
	struct wlr_output *wlr_output = output->output;

	wlr_output_make_current(wlr_output);
	wlr_renderer_begin(sample->renderer, wlr_output);

	if (wlr_output_layout_intersects(sample->layout, output->output,
				sample->x_offs, sample->y_offs,
				sample->x_offs + 128, sample->y_offs + 128)) {
		float matrix[16];

		// transform global coordinates to local coordinates
		int local_x = sample->x_offs;
		int local_y = sample->y_offs;
		wlr_output_layout_output_coords(sample->layout, output->output, &local_x,
				&local_y);

		wlr_texture_get_matrix(sample->cat_texture, &matrix,
			&wlr_output->transform_matrix, local_x, local_y);
		wlr_render_with_matrix(sample->renderer,
			sample->cat_texture, &matrix);
	}

	wlr_renderer_end(sample->renderer);
	wlr_output_swap_buffers(wlr_output);

	if (output->output == sample->main_output) {
		long ms = (ts->tv_sec - output->last_frame.tv_sec) * 1000 +
			(ts->tv_nsec - output->last_frame.tv_nsec) / 1000000;
		// how many seconds have passed since the last frame
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
			struct wlr_output_layout_output *main_l_output;
			main_l_output = wlr_output_layout_get(sample->layout, sample->main_output);
			sample->x_offs = main_l_output->x + 20;
			sample->y_offs = main_l_output->y + 20;
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
	}
}

static inline int max(int a, int b) {
	if (a < b)
		return b;
	return a;
}

static void configure_layout(struct sample_state *sample) {
	wlr_output_layout_destroy(sample->layout);
	sample->layout = wlr_output_layout_init();
	sample->main_output = NULL;
	int max_x = wl_list_empty(&sample->config) ? 0 : INT_MIN;

	// first add all the configure outputs
	struct output_state *output;
	wl_list_for_each(output, &sample->outputs, link) {
		struct output_config *conf;
		wl_list_for_each(conf, &sample->config, link) {
			if (strcmp(conf->name, output->output->name) == 0) {
				wlr_output_layout_add(sample->layout, output->output,
						conf->x, conf->y);
				wlr_output_transform(output->output, conf->transform);
				int width, height;
				wlr_output_effective_resolution(output->output, &width, &height);
				max_x = max(max_x, conf->x + width);

				if (!sample->main_output) {
					sample->main_output = output->output;
					sample->x_offs = conf->x + 20;
					sample->y_offs = conf->y + 20;
				}
				break;
			}
		}
	}

	// now add all the other configured outputs in a sensible position
	wl_list_for_each(output, &sample->outputs, link) {
		if (wlr_output_layout_get(sample->layout, output->output)) {
			continue;
		}
		wlr_output_layout_add(sample->layout, output->output, max_x, 0);
		int width, height;
		wlr_output_effective_resolution(output->output, &width, &height);
		wlr_output_effective_resolution(output->output, &width, &height);
		if (!sample->main_output) {
			sample->main_output = output->output;
			sample->x_offs = max_x + 200;
			sample->y_offs = 200;
		}
		max_x += width;
	}
}

static void handle_output_resolution(struct compositor_state *state, struct output_state *output) {
	struct sample_state *sample = state->data;
	configure_layout(sample);

	// reset the image
	struct wlr_output_layout_output *l_output = wlr_output_layout_get(sample->layout, sample->main_output);
	sample->x_offs = l_output->x + 20;
	sample->y_offs = l_output->y + 20;
}

static void handle_output_add(struct output_state *output) {
	struct sample_state *sample = output->compositor->data;
	wl_list_insert(&sample->outputs, &output->link);
	configure_layout(sample);
}

static void update_velocities(struct compositor_state *state,
		float x_diff, float y_diff) {
	struct sample_state *sample = state->data;
	sample->x_vel += x_diff;
	sample->y_vel += y_diff;
}

static void handle_keyboard_key(struct keyboard_state *kbstate,
		xkb_keysym_t sym, enum wlr_key_state key_state) {
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

static void usage(const char *name, int ret) {
	fprintf(stderr,
		"usage: %s [-d <name> [-r <rotation> | -f]]*\n"
		"\n"
		" -o <output>    The name of the DRM display. e.g. DVI-I-1.\n"
		" -r <rotation>  The rotation counter clockwise. Valid values are 90, 180, 270.\n"
		" -x <position>  The X-axis coordinate position of this output in the layout.\n"
		" -y <position>  The Y-axis coordinate position of this output in the layout.\n"
		" -f             Flip the output along the vertical axis.\n", name);

	exit(ret);
}

static void parse_args(int argc, char *argv[], struct wl_list *config) {
	struct output_config *oc = NULL;

	int c;
	while ((c = getopt(argc, argv, "o:r:x:y:fh")) != -1) {
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
		case 'x':
			if (!oc) {
				fprintf(stderr, "You must specify an output first\n");
				usage(argv[0], 1);
			}
			oc->x = strtol(optarg, NULL, 0);
			break;
		case 'y':
			if (!oc) {
				fprintf(stderr, "You must specify an output first\n");
				usage(argv[0], 1);
			}
			oc->y = strtol(optarg, NULL, 0);
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
	struct sample_state state = {0};

	state.x_vel = 500;
	state.y_vel = 500;
	state.layout = wlr_output_layout_init();

	wl_list_init(&state.config);
	wl_list_init(&state.outputs);
	parse_args(argc, argv, &state.config);

	struct compositor_state compositor = { 0 };
	compositor.data = &state;
	compositor.output_add_cb = handle_output_add;
	compositor.output_frame_cb = handle_output_frame;
	compositor.output_resolution_cb = handle_output_resolution;
	compositor.keyboard_key_cb = handle_keyboard_key;
	compositor_init(&compositor);

	state.renderer = wlr_gles2_renderer_init(compositor.backend);
	state.cat_texture = wlr_render_texture_init(state.renderer);
	wlr_texture_upload_pixels(state.cat_texture, WL_SHM_FORMAT_ABGR8888,
		cat_tex.width, cat_tex.width, cat_tex.height, cat_tex.pixel_data);

	compositor_run(&compositor);

	wlr_texture_destroy(state.cat_texture);
	wlr_renderer_destroy(state.renderer);

	wlr_output_layout_destroy(state.layout);

	struct output_config *ptr, *tmp;
	wl_list_for_each_safe(ptr, tmp, &state.config, link) {
		free(ptr);
	}
}
