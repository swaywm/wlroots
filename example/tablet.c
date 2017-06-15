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
#include "shared.h"
#include "cat.h"

struct sample_state {
	struct wlr_renderer *renderer;
};

static void handle_output_frame(struct output_state *output, struct timespec *ts) {
	struct compositor_state *state = output->compositor;
	struct sample_state *sample = state->data;
	struct wlr_output *wlr_output = output->output;

	wlr_renderer_begin(sample->renderer, wlr_output);

	float matrix[16];
	float color[4] = { 0, 1.0, 0, 1.0 };
	wlr_matrix_scale(&matrix, 128, 128, 1);
	wlr_matrix_mul(&wlr_output->transform_matrix, &matrix, &matrix);
	wlr_render_colored_ellipse(sample->renderer, &color, &matrix);

	wlr_renderer_end(sample->renderer);
}

static void handle_keyboard_key(struct keyboard_state *kbstate,
		xkb_keysym_t sym, enum wlr_key_state key_state) {
	if (sym == XKB_KEY_Escape) {
		kbstate->compositor->exit = true;
	}
}

int main(int argc, char *argv[]) {
	struct sample_state state = { 0 };
	struct compositor_state compositor;

	compositor_init(&compositor);
	compositor.output_frame_cb = handle_output_frame;
	compositor.keyboard_key_cb = handle_keyboard_key;

	state.renderer = wlr_gles3_renderer_init();

	compositor.data = &state;
	compositor_run(&compositor);

	wlr_renderer_destroy(state.renderer);
}
