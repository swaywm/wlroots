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
#include <wlr/types/wlr_input_device.h>
#include <wlr/util/log.h>
#include <xkbcommon/xkbcommon.h>

struct sample_state {
	struct wl_display *display;
	struct wl_listener new_output;
	struct wl_listener new_input;
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

struct sample_keyboard {
	struct sample_state *sample;
	struct wlr_input_device *device;
	struct wl_listener key;
	struct wl_listener destroy;
};

void output_frame_notify(struct wl_listener *listener, void *data) {
	struct sample_output *sample_output =
		wl_container_of(listener, sample_output, frame);
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
	struct sample_output *sample_output =
		wl_container_of(listener, sample_output, destroy);
	wlr_log(WLR_DEBUG, "Output removed");
	wl_list_remove(&sample_output->frame.link);
	wl_list_remove(&sample_output->destroy.link);
	free(sample_output);
}

void new_output_notify(struct wl_listener *listener, void *data) {
	struct wlr_output *output = data;
	struct sample_state *sample =
		wl_container_of(listener, sample, new_output);
	struct sample_output *sample_output =
		calloc(1, sizeof(struct sample_output));
	if (!wl_list_empty(&output->modes)) {
		struct wlr_output_mode *mode =
			wl_container_of(output->modes.prev, mode, link);
		wlr_output_set_mode(output, mode);
	}
	sample_output->output = output;
	sample_output->sample = sample;
	wl_signal_add(&output->events.frame, &sample_output->frame);
	sample_output->frame.notify = output_frame_notify;
	wl_signal_add(&output->events.destroy, &sample_output->destroy);
	sample_output->destroy.notify = output_remove_notify;
}

void keyboard_key_notify(struct wl_listener *listener, void *data) {
	struct sample_keyboard *keyboard = wl_container_of(listener, keyboard, key);
	struct sample_state *sample = keyboard->sample;
	struct wlr_event_keyboard_key *event = data;
	uint32_t keycode = event->keycode + 8;
	const xkb_keysym_t *syms;
	int nsyms = xkb_state_key_get_syms(keyboard->device->keyboard->xkb_state,
			keycode, &syms);
	for (int i = 0; i < nsyms; i++) {
		xkb_keysym_t sym = syms[i];
		if (sym == XKB_KEY_Escape) {
			wl_display_terminate(sample->display);
		}
	}
}

void keyboard_destroy_notify(struct wl_listener *listener, void *data) {
	struct sample_keyboard *keyboard =
		wl_container_of(listener, keyboard, destroy);
	wl_list_remove(&keyboard->destroy.link);
	wl_list_remove(&keyboard->key.link);
	free(keyboard);
}

void new_input_notify(struct wl_listener *listener, void *data) {
	struct wlr_input_device *device = data;
	struct sample_state *sample = wl_container_of(listener, sample, new_input);
	switch (device->type) {
	case WLR_INPUT_DEVICE_KEYBOARD:;
		struct sample_keyboard *keyboard =
			calloc(1, sizeof(struct sample_keyboard));
		keyboard->device = device;
		keyboard->sample = sample;
		wl_signal_add(&device->events.destroy, &keyboard->destroy);
		keyboard->destroy.notify = keyboard_destroy_notify;
		wl_signal_add(&device->keyboard->events.key, &keyboard->key);
		keyboard->key.notify = keyboard_key_notify;
		struct xkb_rule_names rules = { 0 };
		rules.rules = getenv("XKB_DEFAULT_RULES");
		rules.model = getenv("XKB_DEFAULT_MODEL");
		rules.layout = getenv("XKB_DEFAULT_LAYOUT");
		rules.variant = getenv("XKB_DEFAULT_VARIANT");
		rules.options = getenv("XKB_DEFAULT_OPTIONS");
		struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
		if (!context) {
			wlr_log(WLR_ERROR, "Failed to create XKB context");
			exit(1);
		}
		wlr_keyboard_set_keymap(device->keyboard, xkb_map_new_from_names(context,
					&rules, XKB_KEYMAP_COMPILE_NO_FLAGS));
		xkb_context_unref(context);
		break;
	default:
		break;
	}
}

int main(void) {
	wlr_log_init(WLR_DEBUG, NULL);
	struct wl_display *display = wl_display_create();
	struct sample_state state = {
		.color = { 1.0, 0.0, 0.0 },
		.dec = 0,
		.last_frame = { 0 },
		.display = display
	};
	struct wlr_backend *backend = wlr_backend_autocreate(display, NULL);
	if (!backend) {
		exit(1);
	}
	wl_signal_add(&backend->events.new_output, &state.new_output);
	state.new_output.notify = new_output_notify;
	wl_signal_add(&backend->events.new_input, &state.new_input);
	state.new_input.notify = new_input_notify;
	clock_gettime(CLOCK_MONOTONIC, &state.last_frame);

	if (!wlr_backend_start(backend)) {
		wlr_log(WLR_ERROR, "Failed to start backend");
		wlr_backend_destroy(backend);
		exit(1);
	}
	wl_display_run(display);
	wl_display_destroy(display);
}
