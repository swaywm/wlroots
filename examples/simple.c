#define _POSIX_C_SOURCE 200112L

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <wayland-server.h>
#include <xkbcommon/xkbcommon.h>
#include <drm_fourcc.h>

#include <wlr/backend.h>
#include <wlr/backend/session.h>
#include <wlr/render/format_set.h>
#include <wlr/render/renderer.h>
#include <wlr/render/swapchain.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_output.h>
#include <wlr/util/log.h>

struct sample_state {
	struct wl_display *display;
	struct wlr_backend *backend;
	struct wlr_renderer_2 *renderer;

	struct wl_listener new_output;
	struct wl_listener new_input;

	struct timespec last_frame;
	float color[4];
	int dec;
};

struct sample_output {
	struct sample_state *st;
	struct wlr_output *output;

	struct wlr_swapchain *swapchain;

	struct wl_listener frame;
	struct wl_listener destroy;
	struct wl_listener mode;
};

struct sample_keyboard {
	struct sample_state *st;
	struct wlr_input_device *device;

	struct wl_listener key;
	struct wl_listener destroy;
};

static void output_draw(struct sample_output *output) {
	struct sample_state *st = output->st;

	struct wlr_image *img = wlr_swapchain_aquire(output->swapchain);
	if (!img) {
		return;
	}

	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);

	long ms = (now.tv_sec - st->last_frame.tv_sec) * 1000 +
		(now.tv_nsec - st->last_frame.tv_nsec) / 1000000;
	int inc = (st->dec + 1) % 3;

	st->color[inc] += ms / 2000.0f;
	st->color[st->dec] -= ms / 2000.0f;

	if (st->color[st->dec] < 0.0f) {
		st->color[inc] = 1.0f;
		st->color[st->dec] = 0.0f;
		st->dec = inc;
	}

	wlr_renderer_bind_image_2(st->renderer, img);
	wlr_renderer_clear_2(st->renderer, st->color);
	wlr_renderer_flush_2(st->renderer, NULL);

	wlr_output_set_image(output->output, img);
	wlr_output_set_damage(output->output, NULL);
	wlr_output_present(output->output);

	st->last_frame = now;
}

static void output_frame_notify(struct wl_listener *listener, void *data) {
	struct sample_output *output = wl_container_of(listener, output, frame);
	output_draw(output);
}

static void output_remove_notify(struct wl_listener *listener, void *data) {
	struct sample_output *output = wl_container_of(listener, output, destroy);

	wlr_log(WLR_DEBUG, "Output removed");

	wl_list_remove(&output->frame.link);
	wl_list_remove(&output->destroy.link);
	free(output);
}

static void update_swapchain(struct sample_output *output) {
	struct wlr_output *wlr_output = output->output;
	struct sample_state *st = output->st;

	if (output->swapchain) {
		wlr_swapchain_destroy(output->swapchain);
	}

	const struct wlr_format_set *fmts = wlr_output_get_formats(wlr_output);
	const struct wlr_format *fmt = wlr_format_set_get(fmts, DRM_FORMAT_XRGB8888);
	if (!fmt) {
		fmt = wlr_format_set_get(fmts, DRM_FORMAT_ARGB8888);
	}
	if (!fmt) {
		fmt = fmts->formats[0];
	}

	output->swapchain = wlr_swapchain_create(
		wlr_renderer_get_allocator_2(st->renderer),
		wlr_output->backend, wlr_output->width, wlr_output->height,
		fmt->format, fmt->len, fmt->modifiers, 0);

}

static void output_mode_notify(struct wl_listener *listener, void *data) {
	struct sample_output *output = wl_container_of(listener, output, mode);
	update_swapchain(output);
}

static void new_output_notify(struct wl_listener *listener, void *data) {
	struct wlr_output *wlr_output = data;
	struct sample_state *st = wl_container_of(listener, st, new_output);
	wlr_log(WLR_DEBUG, "New output %s", wlr_output->name);

	struct sample_output *output = calloc(1, sizeof(*output));

	if (!wl_list_empty(&wlr_output->modes)) {
		struct wlr_output_mode *mode =
			wl_container_of(wlr_output->modes.prev, mode, link);
		wlr_output_set_mode(wlr_output, mode);
	}

	output->output = wlr_output;
	output->st = st;

	output->frame.notify = output_frame_notify;
	wl_signal_add(&wlr_output->events.frame, &output->frame);

	output->destroy.notify = output_remove_notify;
	wl_signal_add(&wlr_output->events.destroy, &output->destroy);

	output->mode.notify = output_mode_notify;
	wl_signal_add(&wlr_output->events.mode, &output->mode);

	update_swapchain(output);
	//output_draw(output);
}

static void keyboard_key_notify(struct wl_listener *listener, void *data) {
	struct sample_keyboard *kb = wl_container_of(listener, kb, key);
	struct sample_state *st = kb->st;
	struct wlr_event_keyboard_key *event = data;

	uint32_t keycode = event->keycode + 8;
	const xkb_keysym_t *syms;
	int nsyms = xkb_state_key_get_syms(kb->device->keyboard->xkb_state,
			keycode, &syms);
	for (int i = 0; i < nsyms; i++) {
		xkb_keysym_t sym = syms[i];
		if (sym == XKB_KEY_Escape) {
			wl_display_terminate(st->display);
		}
	}
}

static void keyboard_destroy_notify(struct wl_listener *listener, void *data) {
	struct sample_keyboard *kb = wl_container_of(listener, kb, destroy);
	wl_list_remove(&kb->destroy.link);
	wl_list_remove(&kb->key.link);
	free(kb);
}

static void new_input_notify(struct wl_listener *listener, void *data) {
	struct wlr_input_device *device = data;
	struct sample_state *st = wl_container_of(listener, st, new_input);

	switch (device->type) {
	case WLR_INPUT_DEVICE_KEYBOARD: {
		struct sample_keyboard *kb = calloc(1, sizeof(*kb));

		kb->device = device;
		kb->st = st;

		wl_signal_add(&device->events.destroy, &kb->destroy);
		kb->destroy.notify = keyboard_destroy_notify;

		wl_signal_add(&device->keyboard->events.key, &kb->key);
		kb->key.notify = keyboard_key_notify;

		struct xkb_rule_names rules = {
			.rules = getenv("XKB_DEFAULT_RULES"),
			.model = getenv("XKB_DEFAULT_MODEL"),
			.layout = getenv("XKB_DEFAULT_LAYOUT"),
			.variant = getenv("XKB_DEFAULT_VARIANT"),
			.options = getenv("XKB_DEFAULT_OPTIONS"),
		};

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
	}
	default:
		break;
	}
}

int main(void) {
	wlr_log_init(WLR_DEBUG, NULL);

	struct sample_state st = {
		.display = wl_display_create(),
		.color = { 1.0, 0.0, 0.0, 1.0 },
		.dec = 0,
		.last_frame = { 0 },
	};

	st.backend = wlr_backend_autocreate(st.display, NULL);
	if (!st.backend) {
		return 1;
	}

	st.renderer = wlr_renderer_autocreate_2(st.display, st.backend);
	if (!st.renderer) {
		return 1;
	}

	st.new_output.notify = new_output_notify;
	wl_signal_add(&st.backend->events.new_output, &st.new_output);

	st.new_input.notify = new_input_notify;
	wl_signal_add(&st.backend->events.new_input, &st.new_input);

	clock_gettime(CLOCK_MONOTONIC, &st.last_frame);

	if (!wlr_backend_start(st.backend)) {
		wlr_log(WLR_ERROR, "Failed to start backend");
		wlr_backend_destroy(st.backend);
		return 1;
	}

	wl_display_run(st.display);
	wl_display_destroy(st.display);
}
