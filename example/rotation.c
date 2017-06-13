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
	struct wl_list config;
	struct xkb_keymap *keymap;
	struct xkb_state *xkb_state;
	bool exit;

	struct wl_list keyboards;
	struct wl_listener input_add;
	struct wl_listener input_remove;

	struct wl_list outputs;
	struct wl_listener output_add;
	struct wl_listener output_remove;

	struct wlr_renderer *renderer;
	struct wlr_surface *cat_texture;
};

struct output_state {
	struct timespec last_frame;
	struct wl_list link;
	struct wlr_output *output;
	struct state *state;
	struct wl_listener frame;
	float x_offs, y_offs;
	float x_vel, y_vel;
};

struct output_config {
	char *name;
	enum wl_output_transform transform;
	struct wl_list link;
};

struct keyboard_state {
	struct state *state;
	struct wlr_input_device *device;
	struct wl_listener key;
	struct wl_list link;
};

static void output_frame(struct wl_listener *listener, void *data) {
	struct output_state *ostate = wl_container_of(listener, ostate, frame);
	struct wlr_output *output = ostate->output;
	struct state *s = ostate->state;

	int32_t width, height;
	wlr_output_effective_resolution(output, &width, &height);

	wlr_renderer_begin(s->renderer, output);

	float matrix[16];
	for (int y = -128 + (int)ostate->y_offs; y < height; y += 128) {
		for (int x = -128 + (int)ostate->x_offs; x < width; x += 128) {
			wlr_surface_get_matrix(s->cat_texture, &matrix,
				&output->transform_matrix, x, y);
			wlr_render_with_matrix(s->renderer, s->cat_texture, &matrix);
		}
	}

	wlr_renderer_end(s->renderer);

	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	long ms = (now.tv_sec - ostate->last_frame.tv_sec) * 1000 +
		(now.tv_nsec - ostate->last_frame.tv_nsec) / 1000000;
	float seconds = ms / 1000.0f;

	ostate->x_offs += ostate->x_vel * seconds;
	ostate->y_offs += ostate->y_vel * seconds;
	if (ostate->x_offs > 128) ostate->x_offs = 0;
	if (ostate->y_offs > 128) ostate->y_offs = 0;
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
	ostate->x_offs = ostate->y_offs = 0;
	ostate->x_vel = ostate->y_vel = 128;

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

static void update_velocities(struct state *state, float x_diff, float y_diff) {
	struct output_state *ostate;
	wl_list_for_each(ostate, &state->outputs, link) {
		ostate->x_vel += x_diff;
		ostate->y_vel += y_diff;
	}
}

static void handle_keysym(struct state *state, xkb_keysym_t sym,
		enum wlr_key_state key_state) {
	char name[64];
	int l = xkb_keysym_get_name(sym, name, sizeof(name));
	if (l != -1 && l != sizeof(name)) {
		fprintf(stderr, "Key event: %s %s\n", name,
				key_state == WLR_KEY_PRESSED ? "pressed" : "released");
	}
	// NOTE: It may be better to simply refer to our key state during each frame
	// and make this change in pixels/sec^2
	if (key_state == WLR_KEY_PRESSED) {
		switch (sym) {
		case XKB_KEY_Escape:
			state->exit = true;
			break;
		case XKB_KEY_Left:
			update_velocities(state, -16, 0);
			break;
		case XKB_KEY_Right:
			update_velocities(state, 16, 0);
			break;
		case XKB_KEY_Up:
			update_velocities(state, 0, -16);
			break;
		case XKB_KEY_Down:
			update_velocities(state, 0, 16);
			break;
		}
	}
}

static void keyboard_key(struct wl_listener *listener, void *data) {
	struct wlr_keyboard_key *event = data;
	struct keyboard_state *kbstate = wl_container_of(listener, kbstate, key);
	uint32_t keycode = event->keycode + 8;
	const xkb_keysym_t *syms;
	int nsyms = xkb_state_key_get_syms(kbstate->state->xkb_state, keycode, &syms);
	for (int i = 0; i < nsyms; ++i) {
		handle_keysym(kbstate->state, syms[i], event->state);
	}
	xkb_state_update_key(kbstate->state->xkb_state, keycode,
		event->state == WLR_KEY_PRESSED ?  XKB_KEY_DOWN : XKB_KEY_UP);
}

void input_add(struct wl_listener *listener, void *data) {
	struct wlr_input_device *device = data;
	struct state *state = wl_container_of(listener, state, input_add);
	if (device->type != WLR_INPUT_DEVICE_KEYBOARD) {
		return;
	}
	struct keyboard_state *kbstate = calloc(sizeof(struct keyboard_state), 1);
	kbstate->device = device;
	kbstate->state = state;
	wl_list_init(&kbstate->key.link);
	kbstate->key.notify = keyboard_key;
	wl_signal_add(&device->keyboard->events.key, &kbstate->key);
	wl_list_insert(&state->keyboards, &kbstate->link);
}

void input_remove(struct wl_listener *listener, void *data) {
	struct wlr_input_device *device = data;
	struct state *state = wl_container_of(listener, state, input_add);
	if (device->type != WLR_INPUT_DEVICE_KEYBOARD) {
		return;
	}
	struct keyboard_state *kbstate = NULL, *_kbstate;
	wl_list_for_each(_kbstate, &state->keyboards, link) {
		if (_kbstate->device == device) {
			kbstate = kbstate;
			break;
		}
	}
	if (!kbstate) {
		return; // We are unfamiliar with this keyboard
	}
	wl_list_remove(&kbstate->link);
	wl_list_remove(&kbstate->key.link);
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
		.exit = false,
		.input_add = { .notify = input_add },
		.input_remove = { .notify = input_remove },
		.output_add = { .notify = output_add },
		.output_remove = { .notify = output_remove }
	};

	struct xkb_rule_names rules;
	memset(&rules, 0, sizeof(rules));
	rules.rules = getenv("XKB_DEFAULT_RULES");
	rules.model = getenv("XKB_DEFAULT_MODEL");
	rules.layout = getenv("XKB_DEFAULT_LAYOUT");
	rules.variant = getenv("XKB_DEFAULT_VARIANT");
	rules.options = getenv("XKB_DEFAULT_OPTIONS");
	struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	if (!context) {
		fprintf(stderr, "Failed to create XKB context\n");
		return 1;
	}
	state.keymap =
		xkb_map_new_from_names(context, &rules, XKB_KEYMAP_COMPILE_NO_FLAGS);
	if (!state.keymap) {
		fprintf(stderr, "Failed to create XKB keymap\n");
		return 1;
	}
	xkb_context_unref(context);
	state.xkb_state = xkb_state_new(state.keymap);

	wl_list_init(&state.keyboards);
	wl_list_init(&state.input_add.link);
	wl_list_init(&state.input_remove.link);

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

	wl_signal_add(&wlr->events.input_add, &state.input_add);
	wl_signal_add(&wlr->events.input_remove, &state.input_remove);
	wl_signal_add(&wlr->events.output_add, &state.output_add);
	wl_signal_add(&wlr->events.output_remove, &state.output_remove);
	if (!wlr || !wlr_backend_init(wlr)) {
		printf("Failed to initialize backend, bailing out\n");
		return 1;
	}

	state.renderer = wlr_gles3_renderer_init();
	state.cat_texture = wlr_render_surface_init(state.renderer);
	wlr_surface_attach_pixels(state.cat_texture, GL_RGB,
		cat_tex.width, cat_tex.height, cat_tex.pixel_data);

	while (!state.exit) {
		wl_event_loop_dispatch(event_loop, 0);
	}

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
