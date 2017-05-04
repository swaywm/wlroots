#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <wayland-server.h>
#include <GLES3/gl3.h>

#include <wlr/backend/drm.h>
#include <wlr/session.h>

struct state {
	float color[3];
	int dec;

	struct timespec last_frame;

	struct wl_listener add;
	struct wl_listener rem;
	struct wl_listener render;
};

void output_add(struct wl_listener *listener, void *data)
{
	struct wlr_drm_output *out = data;
	fprintf(stderr, "Output '%s' added\n", wlr_drm_output_get_name(out));

	size_t num_modes;
	struct wlr_drm_mode *modes = wlr_drm_output_get_modes(out, &num_modes);

	wlr_drm_output_modeset(out, &modes[0]);
}

void output_rem(struct wl_listener *listener, void *data)
{
	struct wlr_drm_output *out = data;
	fprintf(stderr, "Output '%s' removed\n", wlr_drm_output_get_name(out));
}

void output_render(struct wl_listener *listener, void *data)
{
	struct wlr_drm_output *out = data;
	struct state *s = wl_container_of(listener, s, render);

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

	wlr_drm_output_begin(out);

	glClearColor(s->color[0], s->color[1], s->color[2], 1.0);
	glClear(GL_COLOR_BUFFER_BIT);

	wlr_drm_output_end(out);
}

int timer_done(void *data)
{
	*(bool *)data = true;
	return 1;
}

int main()
{
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
		.add = { .notify = output_add },
		.rem = { .notify = output_rem },
		.render = { .notify = output_render },
	};

	wl_list_init(&state.add.link);
	wl_list_init(&state.rem.link);
	wl_list_init(&state.render.link);
	clock_gettime(CLOCK_MONOTONIC, &state.last_frame);

	struct wl_display *display = wl_display_create();
	struct wl_event_loop *event_loop = wl_display_get_event_loop(display);

	struct wlr_session *session = wlr_session_start();
	if (!session) {
		return 1;
	}

	struct wlr_drm_backend *wlr = wlr_drm_backend_init(display, session,
		&state.add, &state.rem, &state.render);

	bool done = false;
	struct wl_event_source *timer = wl_event_loop_add_timer(event_loop,
		timer_done, &done);

	wl_event_source_timer_update(timer, 10000);

	while (!done)
		wl_event_loop_dispatch(event_loop, 0);

	wl_event_source_remove(timer);
	wlr_drm_backend_free(wlr);
	wl_display_destroy(display);
}
