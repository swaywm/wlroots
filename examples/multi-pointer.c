#define _POSIX_C_SOURCE 200112L
#include <assert.h>
#include <GLES2/gl2.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <wayland-server-protocol.h>
#include <wayland-server.h>
#include <wlr/backend.h>
#include <wlr/backend/session.h>
#include <wlr/render/gles2.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_list.h>
#include <wlr/types/wlr_matrix.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/util/log.h>
#include <wlr/xcursor.h>
#include <xkbcommon/xkbcommon.h>

struct sample_state {
	struct wl_display *display;
	struct wlr_xcursor *xcursor;
	float default_color[4];
	float clear_color[4];
	struct wlr_output_layout *layout;
	struct wl_list cursors; // sample_cursor::link
	struct wl_list pointers; // sample_pointer::link
	struct wl_list outputs; // sample_output::link
	struct timespec last_frame;
	struct wl_listener new_output;
	struct wl_listener new_input;
};

struct sample_cursor {
	struct sample_state *sample;
	struct wlr_input_device *device;
	struct wlr_cursor *cursor;
	struct wl_list link;

	struct wl_listener cursor_motion;
	struct wl_listener cursor_motion_absolute;
	struct wl_listener cursor_button;
	struct wl_listener cursor_axis;
	struct wl_listener destroy;
};

struct sample_pointer {
	struct wlr_input_device *device;
	struct wl_list link;
};

struct sample_output {
	struct sample_state *sample;
	struct wlr_output *output;
	struct wl_listener frame;
	struct wl_listener destroy;
	struct wl_list link;
};

struct sample_keyboard {
	struct sample_state *sample;
	struct wlr_input_device *device;
	struct wl_listener key;
	struct wl_listener destroy;
};

void configure_cursor(struct wlr_cursor *cursor, struct wlr_input_device *device,
		 struct sample_state *sample) {
	struct sample_output *output;
	wlr_log(WLR_ERROR, "Configuring cursor %p for device %p", cursor, device);

	// reset mappings
	wlr_cursor_map_to_output(cursor, NULL);
	wlr_cursor_detach_input_device(cursor, device);
	wlr_cursor_map_input_to_output(cursor, device, NULL);

	wlr_cursor_attach_input_device(cursor, device);

	// configure device to output mappings
	wl_list_for_each(output, &sample->outputs, link) {
		wlr_cursor_map_to_output(cursor, output->output);

		wlr_cursor_map_input_to_output(cursor, device, output->output);
	}
}

void output_frame_notify(struct wl_listener *listener, void *data) {
	struct sample_output *output = wl_container_of(listener, output, frame);
	struct sample_state *sample = output->sample;
	struct wlr_output *wlr_output = output->output;

	wlr_output_make_current(wlr_output, NULL);

	glClearColor(sample->clear_color[0], sample->clear_color[1],
		sample->clear_color[2], sample->clear_color[3]);
	glClear(GL_COLOR_BUFFER_BIT);

	wlr_output_render_software_cursors(wlr_output, NULL);
	wlr_output_swap_buffers(wlr_output, NULL, NULL);
}

static void handle_cursor_motion(struct wl_listener *listener, void *data) {
	struct sample_cursor *cursor =
		wl_container_of(listener, cursor, cursor_motion);
	struct wlr_event_pointer_motion *event = data;
	wlr_cursor_move(cursor->cursor, event->device, event->delta_x,
		event->delta_y);
}

static void handle_cursor_motion_absolute(struct wl_listener *listener,
		void *data) {
	struct sample_cursor *cursor =
		wl_container_of(listener, cursor, cursor_motion_absolute);
	struct wlr_event_pointer_motion_absolute *event = data;
	wlr_cursor_warp_absolute(cursor->cursor, event->device, event->x, event->y);
}

static void cursor_destroy(struct sample_cursor *cursor) {
	wl_list_remove(&cursor->link);
	wl_list_remove(&cursor->cursor_motion.link);
	wl_list_remove(&cursor->cursor_motion_absolute.link);
	wlr_cursor_destroy(cursor->cursor);
	free(cursor);
}

void input_remove_notify(struct wl_listener *listener, void *data) {
	struct wlr_input_device *device = data;
	struct sample_cursor *sample_cursor = wl_container_of(listener, sample_cursor, destroy);
	struct sample_state *sample = sample_cursor->sample;
	struct sample_cursor *cursor;
	wl_list_for_each(cursor, &sample->cursors, link) {
		if (cursor->device == device) {
			cursor_destroy(cursor);
			break;
		}
	}
	struct sample_pointer *pointer;
	wl_list_for_each(pointer, &sample->pointers, link) {
		if (pointer->device == device) {
			free(pointer);
			break;
		}
	}
}

void output_remove_notify(struct wl_listener *listener, void *data) {
	struct sample_output *sample_output = wl_container_of(listener, sample_output, destroy);
	struct sample_state *sample = sample_output->sample;
	wl_list_remove(&sample_output->frame.link);
	wl_list_remove(&sample_output->destroy.link);
	wl_list_remove(&sample_output->link);
	free(sample_output);

	struct sample_cursor *cursor;
	wl_list_for_each(cursor, &sample->cursors, link) {
		configure_cursor(cursor->cursor, cursor->device, sample);
	}
}

void new_output_notify(struct wl_listener *listener, void *data) {
	struct wlr_output *output = data;
	struct sample_state *sample = wl_container_of(listener, sample, new_output);
	struct sample_output *sample_output = calloc(1, sizeof(struct sample_output));
	if (!wl_list_empty(&output->modes)) {
		struct wlr_output_mode *mode = wl_container_of(output->modes.prev, mode, link);
		wlr_output_set_mode(output, mode);
	}
	sample_output->output = output;
	sample_output->sample = sample;
	wl_signal_add(&output->events.frame, &sample_output->frame);
	sample_output->frame.notify = output_frame_notify;
	wl_signal_add(&output->events.destroy, &sample_output->destroy);
	sample_output->destroy.notify = output_remove_notify;

	wlr_output_layout_add_auto(sample->layout, output);


	struct sample_cursor *cursor;
	wl_list_for_each(cursor, &sample->cursors, link) {
		configure_cursor(cursor->cursor, cursor->device, sample);

		struct wlr_xcursor_image *image = sample->xcursor->images[0];
		wlr_cursor_set_image(cursor->cursor, image->buffer, image->width * 4,
			image->width, image->height, image->hotspot_x, image->hotspot_y, 0);

		wlr_cursor_warp(cursor->cursor, NULL, cursor->cursor->x,
			cursor->cursor->y);
	}
	wl_list_insert(&sample->outputs, &sample_output->link);
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
	struct sample_keyboard *keyboard = wl_container_of(listener, keyboard, destroy);
	wl_list_remove(&keyboard->destroy.link);
	wl_list_remove(&keyboard->key.link);
	free(keyboard);
}

void new_input_notify(struct wl_listener *listener, void *data) {
	struct wlr_input_device *device = data;
	struct sample_state *sample = wl_container_of(listener, sample, new_input);
	switch (device->type) {
	case WLR_INPUT_DEVICE_KEYBOARD:;
		struct sample_keyboard *keyboard = calloc(1, sizeof(struct sample_keyboard));
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
		struct xkb_keymap *keymap = xkb_map_new_from_names(context, &rules,
			XKB_KEYMAP_COMPILE_NO_FLAGS);
		if (!keymap) {
			wlr_log(WLR_ERROR, "Failed to create XKB keymap");
			exit(1);
		}
		wlr_keyboard_set_keymap(device->keyboard, keymap);
		xkb_keymap_unref(keymap);
		xkb_context_unref(context);
		break;
	case WLR_INPUT_DEVICE_POINTER:;
		struct sample_cursor *cursor = calloc(1, sizeof(struct sample_cursor));
		struct sample_pointer *pointer = calloc(1, sizeof(struct sample_pointer));
		pointer->device = device;
		cursor->sample = sample;
		cursor->device = device;

		cursor->cursor = wlr_cursor_create();

		wlr_cursor_attach_output_layout(cursor->cursor, sample->layout);

		wl_signal_add(&cursor->cursor->events.motion, &cursor->cursor_motion);
		cursor->cursor_motion.notify = handle_cursor_motion;
		wl_signal_add(&cursor->cursor->events.motion_absolute,
			&cursor->cursor_motion_absolute);
		cursor->cursor_motion_absolute.notify = handle_cursor_motion_absolute;

		wlr_cursor_attach_input_device(cursor->cursor, device);
		configure_cursor(cursor->cursor, device, sample);

		struct wlr_xcursor_image *image = sample->xcursor->images[0];
		wlr_cursor_set_image(cursor->cursor, image->buffer, image->width * 4,
			image->width, image->height, image->hotspot_x, image->hotspot_y, 0);

		wl_list_insert(&sample->cursors, &cursor->link);
		wl_list_insert(&sample->pointers, &pointer->link);
		break;
	default:
		break;
	}
}

int main(int argc, char *argv[]) {
	wlr_log_init(WLR_DEBUG, NULL);
	struct wl_display *display = wl_display_create();
	struct sample_state state = {
		.default_color = { 0.25f, 0.25f, 0.25f, 1 },
		.clear_color = { 0.25f, 0.25f, 0.25f, 1 },
		.display = display,
	};
	struct wlr_backend *wlr = wlr_backend_autocreate(display, NULL);
	if (!wlr) {
		exit(1);
	}
	wl_list_init(&state.cursors);
	wl_list_init(&state.pointers);
	wl_list_init(&state.outputs);

	state.layout = wlr_output_layout_create();

	wl_signal_add(&wlr->events.new_output, &state.new_output);
	state.new_output.notify = new_output_notify;
	wl_signal_add(&wlr->events.new_input, &state.new_input);
	state.new_input.notify = new_input_notify;

	clock_gettime(CLOCK_MONOTONIC, &state.last_frame);

	struct wlr_xcursor_theme *theme = wlr_xcursor_theme_load("default", 16);
	if (!theme) {
		wlr_log(WLR_ERROR, "Failed to load cursor theme");
		return 1;
	}
	state.xcursor = wlr_xcursor_theme_get_cursor(theme, "left_ptr");
	if (!state.xcursor) {
		wlr_log(WLR_ERROR, "Failed to load left_ptr cursor");
		return 1;
	}

	if (!wlr_backend_start(wlr)) {
		wlr_log(WLR_ERROR, "Failed to start backend");
		wlr_backend_destroy(wlr);
		exit(1);
	}
	wl_display_run(display);
	wl_display_destroy(display);

	struct sample_cursor *cursor, *tmp_cursor;
	wl_list_for_each_safe(cursor, tmp_cursor, &state.cursors, link) {
		cursor_destroy(cursor);
	}

	struct sample_pointer *pointer, *tmp_pointer;
	wl_list_for_each_safe(pointer, tmp_pointer, &state.pointers, link) {
		free(pointer);
	}

	wlr_xcursor_theme_destroy(theme);
	wlr_output_layout_destroy(state.layout);
}
