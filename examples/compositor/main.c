#define _POSIX_C_SOURCE 199309L
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <inttypes.h>
#include <wayland-server.h>
#include <wlr/backend.h>
#include <wlr/backend/session.h>
#include <wlr/render.h>
#include <wlr/render/gles2.h>
#include <wlr/types/wlr_output.h>
#include <xkbcommon/xkbcommon.h>
#include <wlr/util/log.h>
#include "shared.h"
#include "compositor.h"
#include "wlr_surface.h"

struct sample_state {
	struct wlr_renderer *renderer;
	struct wl_compositor_state compositor;
	struct wl_shell_state shell;
};

void handle_output_frame(struct output_state *output, struct timespec *ts) {
	struct compositor_state *state = output->compositor;
	struct sample_state *sample = state->data;
	struct wlr_output *wlr_output = output->output;

	wlr_output_make_current(wlr_output);
	wlr_renderer_begin(sample->renderer, wlr_output);

	struct wl_resource *_res;
	float matrix[16];
	wl_list_for_each(_res, &sample->compositor.surfaces, link) {
		struct wlr_surface *surface = wl_resource_get_user_data(_res);
		if (surface->texture->valid) {
			wlr_texture_get_matrix(surface->texture, &matrix,
					&wlr_output->transform_matrix, 200, 200);
			wlr_render_with_matrix(sample->renderer, surface->texture, &matrix);
		}
	}

	wlr_renderer_end(sample->renderer);
	wlr_output_swap_buffers(wlr_output);
}

int main() {
	struct sample_state state = { 0 };
	struct compositor_state compositor = { 0,
		.data = &state,
		.output_frame_cb = handle_output_frame,
	};
	compositor_init(&compositor);

	state.renderer = wlr_gles2_renderer_init();
	wl_display_init_shm(compositor.display);
	wl_compositor_init(compositor.display, &state.compositor, state.renderer);
	wl_shell_init(compositor.display, &state.shell);

	compositor_run(&compositor);
}
