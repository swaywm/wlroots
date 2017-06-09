#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <inttypes.h>
#include <wayland-server.h>
#include <GLES3/gl3.h>
#include <wlr/backend.h>
#include <wlr/session.h>
#include <wlr/types.h>

struct state {
	float color[3];
	int dec;
	struct timespec last_frame;

	struct wl_list keyboards;
	struct wl_listener input_add;
	struct wl_listener input_remove;

	struct wl_listener output_add;
	struct wl_listener output_remove;
	struct wl_list outputs;
};

struct output_state {
	struct state *state;
	struct wlr_output *output;
	struct wl_listener frame;
	struct wl_list link;
};

struct keyboard_state {
	struct state *state;
	struct wlr_input_device *device;
	struct wl_listener key;
	struct wl_listener mods;
	struct wl_list link;
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
	fprintf(stderr, "%s %s %"PRId32"mm x %"PRId32"mm\n", output->make, output->model,
		output->phys_width, output->phys_height);
	wlr_output_set_mode(output, output->modes->items[0]);
	struct output_state *ostate = calloc(1, sizeof(struct output_state));
	ostate->output = output;
	ostate->state = state;
	ostate->frame.notify = output_frame;
	wl_list_init(&ostate->frame.link);
	wl_signal_add(&output->events.frame, &ostate->frame);
	wl_list_insert(&state->outputs, &ostate->link);
}

void output_remove(struct wl_listener *listener, void *data) {
	struct wlr_output *output = data;
	struct state *state = wl_container_of(listener, state, output_remove);
	struct output_state *ostate = NULL, *_ostate;
	wl_list_for_each(_ostate, &state->outputs, link) {
		if (_ostate->output == output) {
			ostate = _ostate;
			break;
		}
	}
	if (!ostate) {
		return; // We are unfamiliar with this output
	}
	wl_list_remove(&ostate->link);
	wl_list_remove(&ostate->frame.link);
}

static void keyboard_key(struct wl_listener *listener, void *data) {
	struct wlr_keyboard_key *event = data;
	struct keyboard_state *kbstate = wl_container_of(listener, kbstate, key);
	fprintf(stderr, "Key event: %u %s\n", event->keycode,
			event->state == WLR_KEY_PRESSED ? "pressed" : "released");
}

void input_add(struct wl_listener *listener, void *data) {
	struct wlr_input_device *device = data;
	struct state *state = wl_container_of(listener, state, input_add);
	if (device->type != WLR_INPUT_DEVICE_KEYBOARD) {
		return;
	}
	fprintf(stderr, "Keyboard '%s' (%d:%d) added\n", device->name,
			device->vendor, device->product);
	struct keyboard_state *kbstate = calloc(sizeof(struct keyboard_state), 1);
	kbstate->device = device;
	wl_list_init(&kbstate->key.link);
	wl_list_init(&kbstate->mods.link);
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
	//wl_list_remove(&kbstate->key.link);
	//wl_list_remove(&kbstate->mods.link);
}

int timer_done(void *data) {
	*(bool *)data = true;
	return 1;
}

int main() {
	struct state state = {
		.color = { 1.0, 0.0, 0.0 },
		.dec = 0,
		.input_add = { .notify = input_add },
		.input_remove = { .notify = input_remove },
		.output_add = { .notify = output_add },
		.output_remove = { .notify = output_remove }
	};

	wl_list_init(&state.keyboards);
	wl_list_init(&state.input_add.link);
	wl_list_init(&state.input_remove.link);

	wl_list_init(&state.outputs);
	wl_list_init(&state.output_remove.link);
	wl_list_init(&state.output_remove.link);
	clock_gettime(CLOCK_MONOTONIC, &state.last_frame);

	struct wl_display *display = wl_display_create();
	struct wl_event_loop *event_loop = wl_display_get_event_loop(display);

	struct wlr_session *session = wlr_session_start(display);
	if (!session) {
		return 1;
	}

	struct wlr_backend *wlr = wlr_backend_autocreate(display, session);
	wl_signal_add(&wlr->events.input_add, &state.input_add);
	wl_signal_add(&wlr->events.input_remove, &state.input_remove);
	wl_signal_add(&wlr->events.output_add, &state.output_add);
	wl_signal_add(&wlr->events.output_remove, &state.output_remove);
	if (!wlr || !wlr_backend_init(wlr)) {
		return 1;
	}

	bool done = false;
	struct wl_event_source *timer = wl_event_loop_add_timer(event_loop,
		timer_done, &done);

	wl_event_source_timer_update(timer, 10000);

	while (!done) {
		wl_event_loop_dispatch(event_loop, 0);
	}

	wl_event_source_remove(timer);
	wlr_backend_destroy(wlr);
	wl_display_destroy(display);
}
