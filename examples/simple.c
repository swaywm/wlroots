#define _POSIX_C_SOURCE 199309L
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <inttypes.h>
#include <wayland-server.h>
#include <GLES2/gl2.h>
#include <wlr/backend.h>
#include <wlr/backend/session.h>
#include <wlr/types/wlr_output.h>
#include <wlr/util/log.h>
#include <xkbcommon/xkbcommon.h>
#include "support/shared.h"

struct sample_state {
	float color[3];
	int dec;
};

void handle_output_frame(struct output_state *output, struct timespec *ts) {
	struct compositor_state *state = output->compositor;
	struct sample_state *sample = state->data;

	long ms = (ts->tv_sec - state->last_frame.tv_sec) * 1000 +
		(ts->tv_nsec - state->last_frame.tv_nsec) / 1000000;
	int inc = (sample->dec + 1) % 3;

	sample->color[inc] += ms / 2000.0f;
	sample->color[sample->dec] -= ms / 2000.0f;

	if (sample->color[sample->dec] < 0.0f) {
		sample->color[inc] = 1.0f;
		sample->color[sample->dec] = 0.0f;
		sample->dec = inc;
	}

	wlr_output_make_current(output->output);

	glClearColor(sample->color[0], sample->color[1], sample->color[2], 1.0);
	glClear(GL_COLOR_BUFFER_BIT);

	wlr_output_swap_buffers(output->output, NULL, NULL);
}

int main() {
	wlr_log_init(L_DEBUG, NULL);
	struct sample_state state = {
		.color = { 1.0, 0.0, 0.0 },
		.dec = 0,
	};
	struct compositor_state compositor = { 0,
		.data = &state,
		.output_frame_cb = handle_output_frame,
	};
	compositor_init(&compositor);
	if (!wlr_backend_start(compositor.backend)) {
		wlr_log(L_ERROR, "Failed to start backend");
		wlr_backend_destroy(compositor.backend);
		exit(1);
	}
	wl_display_run(compositor.display);
	compositor_fini(&compositor);
}
