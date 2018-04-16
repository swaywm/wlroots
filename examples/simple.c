#define _POSIX_C_SOURCE 200112L
#include <GLES2/gl2.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wayland-server.h>
#include <wlr/backend.h>
#include <wlr/backend/session.h>
#include <wlr/types/wlr_output.h>
#include <wlr/util/log.h>
#include <xkbcommon/xkbcommon.h>

struct sample_state {
	struct wl_listener new_output;
	struct timespec last_frame;
	float color[3];
	int dec;
};

struct sample_output {
	struct sample_state *sample;
	struct wlr_output *output;
	struct wl_listener frame;
	struct wl_listener destroy;
};


void output_frame_notify(struct wl_listener *listener, void *data) {
	struct sample_output *sample_output = wl_container_of(listener, sample_output, frame);
	struct sample_state *sample = sample_output->sample;
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);

	long ms = (now.tv_sec - sample->last_frame.tv_sec) * 1000 +
		(now.tv_nsec - sample->last_frame.tv_nsec) / 1000000;
	int inc = (sample->dec + 1) % 3;

	sample->color[inc] += ms / 2000.0f;
	sample->color[sample->dec] -= ms / 2000.0f;

	if (sample->color[sample->dec] < 0.0f) {
		sample->color[inc] = 1.0f;
		sample->color[sample->dec] = 0.0f;
		sample->dec = inc;
	}

	wlr_output_make_current(sample_output->output, NULL);

	glClearColor(sample->color[0], sample->color[1], sample->color[2], 1.0);
	glClear(GL_COLOR_BUFFER_BIT);

	wlr_output_swap_buffers(sample_output->output, NULL, NULL);
	sample->last_frame = now;
}

void output_remove_notify(struct wl_listener *listener, void *data) {
	struct sample_output *sample_output = wl_container_of(listener, sample_output, destroy);
	wl_list_remove(&sample_output->frame.link);
	free(sample_output);
}

void new_output_notify(struct wl_listener *listener, void *data) {
	struct wlr_output *output = data;
	struct sample_state *sample = wl_container_of(listener, sample, new_output);
	struct sample_output *sample_output = calloc(1, sizeof(struct sample_output));
	sample_output->output = output;
	sample_output->sample = sample;
	wl_signal_add(&output->events.frame, &sample_output->frame);
	sample_output->frame.notify = output_frame_notify;
	wl_signal_add(&output->events.destroy, &sample_output->destroy);
	sample_output->destroy.notify = output_remove_notify;
}

int main() {
	wlr_log_init(L_DEBUG, NULL);
	struct sample_state state = {
		.color = { 1.0, 0.0, 0.0 },
		.dec = 0,
		.last_frame = { 0 }
	};
	struct wl_display *display = wl_display_create();
	struct wlr_backend *wlr = wlr_backend_autocreate(display);
	if (!wlr) {
		exit(1);
	}
	wl_signal_add(&wlr->events.new_output, &state.new_output);
	state.new_output.notify = new_output_notify;
	clock_gettime(CLOCK_MONOTONIC, &state.last_frame);
	const char *socket = wl_display_add_socket_auto(display);
	if (!socket) {
		wlr_log_errno(L_ERROR, "Unable to open wayland socket");
		wlr_backend_destroy(wlr);
		exit(1);
	}
	setenv("_WAYLAND_DISPLAY", socket, true);
	if (!wlr_backend_start(wlr)) {
		wlr_log(L_ERROR, "Failed to start backend");
		wlr_backend_destroy(wlr);
		exit(1);
	}
	wl_display_run(display);
	wl_display_destroy(display);
}
