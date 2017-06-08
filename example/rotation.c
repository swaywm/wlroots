#define _POSIX_C_SOURCE 199309L
#define _XOPEN_SOURCE 500
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <unistd.h>
#include <wayland-server.h>
#include <wayland-server-protocol.h>
#include <GLES3/gl3.h>
#include <wlr/render/matrix.h>
#include <wlr/render/gles3.h>
#include <wlr/render.h>
#include <wlr/backend.h>
#include <wlr/session.h>
#include <wlr/types.h>
#include <math.h>
#include "cat.h"

struct state {
	struct wl_listener output_add;
	struct wl_listener output_remove;
	struct wl_list outputs;
	struct wl_list config;
	struct wlr_renderer *renderer;
	struct wlr_surface *cat_texture;
};

struct output_state {
	struct timespec last_frame;
	struct wl_list link;
	struct wlr_output *output;
	struct state *state;
	struct wl_listener frame;
	float x, y;
	float vel_x, vel_y;
};

struct output_config {
	char *name;
	enum wl_output_transform transform;
	struct wl_list link;
};

static void output_frame(struct wl_listener *listener, void *data) {
	struct output_state *ostate = wl_container_of(listener, ostate, frame);
	struct wlr_output *output = ostate->output;
	struct state *s = ostate->state;

	wlr_renderer_begin(s->renderer, output);

	float matrix[16];
	wlr_surface_get_matrix(s->cat_texture, &matrix,
		&output->transform_matrix, ostate->x, ostate->y);
	wlr_render_with_matrix(s->renderer, s->cat_texture, &matrix);

	wlr_renderer_end(s->renderer);

	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	long ms = (now.tv_sec - ostate->last_frame.tv_sec) * 1000 +
		(now.tv_nsec - ostate->last_frame.tv_nsec) / 1000000;
	float seconds = ms / 1000.0f;

	int32_t width, height;
	wlr_output_effective_resolution(output, &width, &height);
	ostate->x += ostate->vel_x * seconds;
	ostate->y += ostate->vel_y * seconds;
	if (ostate->y > height - 128) {
		ostate->vel_y = -ostate->vel_y;
	} else {
		ostate->vel_y += 50 * seconds;
	}
	if (ostate->x > width - 128 || ostate->x < 0) {
		ostate->vel_x = -ostate->vel_x;
	}
	ostate->last_frame = now;
}

static void output_add(struct wl_listener *listener, void *data) {
	struct wlr_output *output = data;
	struct state *state = wl_container_of(listener, state, output_add);

	fprintf(stderr, "Output '%s' added\n", output->name);
	wlr_output_set_mode(output, output->modes->items[0]);

	struct output_state *ostate = calloc(1, sizeof(struct output_state));

	clock_gettime(CLOCK_MONOTONIC, &ostate->last_frame);
	ostate->output = output;
	ostate->state = state;
	ostate->frame.notify = output_frame;
	ostate->x = ostate->y = 0;
	ostate->vel_x = 100;
	ostate->vel_y = 0;

	struct output_config *conf;
	wl_list_for_each(conf, &state->config, link) {
		if (strcmp(conf->name, output->name) == 0) {
			wlr_output_transform(ostate->output, conf->transform);
			break;
		}
	}

	wl_list_init(&ostate->frame.link);
	wl_signal_add(&output->events.frame, &ostate->frame);
	wl_list_insert(&state->outputs, &ostate->link);
}

static void output_remove(struct wl_listener *listener, void *data) {
	struct wlr_output *output = data;
	struct state *state = wl_container_of(listener, state, output_remove);
	struct output_state *ostate;

	wl_list_for_each(ostate, &state->outputs, link) {
		if (ostate->output == output) {
			wl_list_remove(&ostate->link);
			wl_list_remove(&ostate->frame.link);
			free(ostate);
			break;
		}
	}
}

static int timer_done(void *data) {
	*(bool *)data = true;
	return 1;
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
	struct state state = {
		.output_add = { .notify = output_add },
		.output_remove = { .notify = output_remove }
	};

	wl_list_init(&state.outputs);
	wl_list_init(&state.config);
	wl_list_init(&state.output_add.link);
	wl_list_init(&state.output_remove.link);

	parse_args(argc, argv, &state.config);

	struct wl_display *display = wl_display_create();
	struct wl_event_loop *event_loop = wl_display_get_event_loop(display);

	struct wlr_session *session = wlr_session_start(display);
	if (!session) {
		return 1;
	}

	struct wlr_backend *wlr = wlr_backend_autocreate(display, session);
	if (!wlr) {
		return 1;
	}

	wl_signal_add(&wlr->events.output_add, &state.output_add);
	wl_signal_add(&wlr->events.output_remove, &state.output_remove);

	if (!wlr_backend_init(wlr)) {
		return 1;
	}

	state.renderer = wlr_gles3_renderer_init();
	state.cat_texture = wlr_render_surface_init(state.renderer);
	wlr_surface_attach_pixels(state.cat_texture, GL_RGB,
		cat_tex.width, cat_tex.height, cat_tex.pixel_data);

	bool done = false;
	struct wl_event_source *timer = wl_event_loop_add_timer(event_loop,
		timer_done, &done);

	wl_event_source_timer_update(timer, 30000);

	while (!done) {
		wl_event_loop_dispatch(event_loop, 0);
	}

	wl_event_source_remove(timer);
	wlr_backend_destroy(wlr);
	wlr_session_finish(session);
	wlr_surface_destroy(state.cat_texture);
	wlr_renderer_destroy(state.renderer);
	wl_display_destroy(display);

	struct output_config *ptr, *tmp;
	wl_list_for_each_safe(ptr, tmp, &state.config, link) {
		free(ptr);
	}
}
