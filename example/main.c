#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <wayland-server.h>
#include <GLES3/gl3.h>
#include <wlr/backend.h>
#include <wlr/session.h>
#include <wlr/wayland.h>
#include <wlr/common/list.h>

struct state {
	float color[3];
	int dec;
	struct timespec last_frame;
	struct wl_listener output_add;
	struct wl_listener output_remove;
	list_t *outputs;
};

struct output_state {
	struct wlr_output *output;
	struct state *state;
	struct wl_listener frame;
};

void output_frame(struct wl_listener *listener, void *data) {
	struct output_state *ostate = wl_container_of(listener, ostate, frame);
	struct state *s = ostate->state;

	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);

	long ms = (now.tv_sec - s->last_frame.tv_sec) * 1000 +
		(now.tv_nsec - s->last_frame.tv_nsec) / 1000000;
	int inc = (s->dec + 1) % 3;

	s->color[inc] += ms / 2000.0f;
	s->color[s->dec] -= ms / 2000.0f;

	if (s->color[s->dec] < 0.0f) {
		s->color[inc] = 1.0f;
		s->color[s->dec] = 0.0f;
		s->dec = inc;
	}

	s->last_frame = now;

	glClearColor(s->color[0], s->color[1], s->color[2], 1.0);
	glClear(GL_COLOR_BUFFER_BIT);
}

void output_add(struct wl_listener *listener, void *data) {
	struct wlr_output *output = data;
	struct state *state = wl_container_of(listener, state, output_add);
	fprintf(stderr, "Output '%s' added\n", output->name);
	wlr_output_set_mode(output, output->modes->items[0]);
	struct output_state *ostate = calloc(1, sizeof(struct output_state));
	ostate->output = output;
	ostate->state = state;
	ostate->frame.notify = output_frame;
	wl_list_init(&ostate->frame.link);
	wl_signal_add(&output->events.frame, &ostate->frame);
	list_add(state->outputs, ostate);
}

void output_remove(struct wl_listener *listener, void *data) {
	struct wlr_output *output = data;
	fprintf(stderr, "Output '%s' removed\n", output->name);
	// TODO: remove signal from state->output_frame
}

int timer_done(void *data) {
	*(bool *)data = true;
	return 1;
}

int enable_outputs(void *data) {
	struct state *state = data;
	for (size_t i = 0; i < state->outputs->length; ++i) {
		struct output_state *ostate = state->outputs->items[i];
		struct wlr_output *output = ostate->output;
		wlr_output_enable(output, true);
	}
	return 1;
}

int disable_outputs(void *data) {
	struct state *state = data;
	for (size_t i = 0; i < state->outputs->length; ++i) {
		struct output_state *ostate = state->outputs->items[i];
		struct wlr_output *output = ostate->output;
		wlr_output_enable(output, false);
	}
	return 1;
}

int main() {
	if (getenv("DISPLAY")) {
		fprintf(stderr, "Detected that X is running. Run this in its own virtual terminal.\n");
		return 1;
	} else if (getenv("WAYLAND_DISPLAY")) {
		fprintf(stderr, "Detected that Wayland is running. Run this in its own virtual terminal.\n");
		return 1;
	}

	struct state state = {
		.color = { 1.0, 0.0, 0.0 },
		.dec = 0,
		.output_add = { .notify = output_add },
		.output_remove = { .notify = output_remove },
		.outputs = list_create(),
	};

	wl_list_init(&state.output_add.link);
	wl_list_init(&state.output_remove.link);
	clock_gettime(CLOCK_MONOTONIC, &state.last_frame);

	struct wl_display *display = wl_display_create();
	struct wl_event_loop *event_loop = wl_display_get_event_loop(display);

	struct wlr_session *session = wlr_session_start();
	if (!session) {
		return 1;
	}

	struct wlr_backend *wlr = wlr_backend_autocreate(display, session);
	wl_signal_add(&wlr->events.output_add, &state.output_add);
	wl_signal_add(&wlr->events.output_remove, &state.output_remove);
	if (!wlr || !wlr_backend_init(wlr)) {
		return 1;
	}

	bool done = false;
	struct wl_event_source *timer = wl_event_loop_add_timer(event_loop,
		timer_done, &done);
	struct wl_event_source *timer_disable_outputs =
		wl_event_loop_add_timer(event_loop, disable_outputs, &state);
	struct wl_event_source *timer_enable_outputs =
		wl_event_loop_add_timer(event_loop, enable_outputs, &state);

	wl_event_source_timer_update(timer, 20000);
	wl_event_source_timer_update(timer_disable_outputs, 5000);
	wl_event_source_timer_update(timer_enable_outputs, 10000);

	while (!done) {
		wl_event_loop_dispatch(event_loop, 0);
	}

	wl_event_source_remove(timer);
	wlr_backend_destroy(wlr);
	wl_display_destroy(display);
}
