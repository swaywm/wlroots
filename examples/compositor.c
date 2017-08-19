#define _POSIX_C_SOURCE 199309L
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <inttypes.h>
#include <unistd.h>
#include <sys/mman.h>
#include <wayland-server.h>
#include <wlr/backend.h>
#include <wlr/backend/session.h>
#include <wlr/render.h>
#include <wlr/render/matrix.h>
#include <wlr/render/gles2.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_surface.h>
#include <wlr/types/wlr_wl_shell.h>
#include <wlr/types/wlr_xdg_shell_v6.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_data_device_manager.h>
#include "wlr/types/wlr_compositor.h"
#include <xkbcommon/xkbcommon.h>
#include <wlr/util/log.h>
#include "shared.h"

// TODO: move to common header?
int os_create_anonymous_file(off_t size);

struct sample_state {
	struct wlr_renderer *renderer;
	struct wlr_compositor compositor;
	struct wlr_wl_shell *wl_shell;
	struct wlr_seat *wl_seat;
	struct wlr_xdg_shell_v6 *xdg_shell;
	struct wlr_data_device_manager *data_device_manager;
	struct wl_resource *focus;
	struct wl_listener keyboard_bound;
	int keymap_fd;
	size_t keymap_size;
	uint32_t serial;
};

/*
 * Convert timespec to milliseconds
 */
static inline int64_t timespec_to_msec(const struct timespec *a) {
	return (int64_t)a->tv_sec * 1000 + a->tv_nsec / 1000000;
}

static void output_frame_handle_surface(struct sample_state *sample,
		struct wlr_output *wlr_output, struct timespec *ts,
		struct wl_resource *_res) {
	struct wlr_surface *surface = wl_resource_get_user_data(_res);
	float matrix[16];
	float transform[16];
	wlr_surface_flush_damage(surface);
	if (surface->texture->valid) {
		wlr_matrix_translate(&transform, 200, 200, 0);
		wlr_surface_get_matrix(surface, &matrix,
			&wlr_output->transform_matrix, &transform);
		wlr_render_with_matrix(sample->renderer, surface->texture, &matrix);

		struct wlr_frame_callback *cb, *cnext;
		wl_list_for_each_safe(cb, cnext, &surface->frame_callback_list, link) {
			wl_callback_send_done(cb->resource, timespec_to_msec(ts));
			wl_resource_destroy(cb->resource);
		}
	}
}
static void handle_output_frame(struct output_state *output, struct timespec *ts) {
	struct compositor_state *state = output->compositor;
	struct sample_state *sample = state->data;
	struct wlr_output *wlr_output = output->output;

	wlr_output_make_current(wlr_output);
	wlr_renderer_begin(sample->renderer, wlr_output);

	struct wlr_wl_shell_surface *wl_shell_surface;
	wl_list_for_each(wl_shell_surface, &sample->wl_shell->surfaces, link) {
		output_frame_handle_surface(sample, wlr_output, ts, wl_shell_surface->surface);
	}
	struct wlr_xdg_surface_v6 *xdg_surface;
	wl_list_for_each(xdg_surface, &sample->xdg_shell->surfaces, link) {
		output_frame_handle_surface(sample, wlr_output, ts, xdg_surface->surface);
	}

	wlr_renderer_end(sample->renderer);
	wlr_output_swap_buffers(wlr_output);
}

static void handle_keyboard_key(struct keyboard_state *keyboard, uint32_t keycode,
	 	xkb_keysym_t sym, enum wlr_key_state key_state) {
	struct compositor_state *state = keyboard->compositor;
	struct sample_state *sample = state->data;

	struct wl_resource *res = NULL;
	struct wlr_seat_handle *seat_handle = NULL;
	wl_list_for_each(res, &sample->compositor.surfaces, link) {
		break;
	}

	if (res) {
		seat_handle = wlr_seat_handle_for_client(sample->wl_seat,
			wl_resource_get_client(res));
	}

	if (res != sample->focus && seat_handle && seat_handle->keyboard) {
		struct wl_array keys;
		wl_array_init(&keys);
		wl_keyboard_send_enter(seat_handle->keyboard, ++sample->serial, res, &keys);
		sample->focus = res;
	}

	if (seat_handle && seat_handle->keyboard) {
		uint32_t depressed = xkb_state_serialize_mods(keyboard->xkb_state,
			XKB_STATE_MODS_DEPRESSED);
		uint32_t latched = xkb_state_serialize_mods(keyboard->xkb_state,
			XKB_STATE_MODS_LATCHED);
		uint32_t locked = xkb_state_serialize_mods(keyboard->xkb_state,
			XKB_STATE_MODS_LOCKED);
		uint32_t group = xkb_state_serialize_layout(keyboard->xkb_state,
			XKB_STATE_LAYOUT_EFFECTIVE);
		wl_keyboard_send_modifiers(seat_handle->keyboard, ++sample->serial, depressed,
			latched, locked, group);
		wl_keyboard_send_key(seat_handle->keyboard, ++sample->serial, 0, keycode, key_state);
	}
}

static void handle_keyboard_bound(struct wl_listener *listener, void *data) {
	struct wlr_seat_handle *handle = data;
	struct sample_state *state = wl_container_of(listener, state, keyboard_bound);

	wl_keyboard_send_keymap(handle->keyboard, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1,
		state->keymap_fd, state->keymap_size);

	if (wl_resource_get_version(handle->keyboard) >= 2) {
		wl_keyboard_send_repeat_info(handle->keyboard, 25, 600);
	}
}

int main() {
	struct sample_state state = { 0 };
	struct compositor_state compositor = { 0,
		.data = &state,
		.output_frame_cb = handle_output_frame,
		.keyboard_key_cb = handle_keyboard_key
	};
	compositor_init(&compositor);

	state.renderer = wlr_gles2_renderer_create(compositor.backend);
	if (!state.renderer) {
		wlr_log(L_ERROR, "Could not start compositor, OOM");
		exit(EXIT_FAILURE);
	}
	wl_display_init_shm(compositor.display);
	wlr_compositor_init(&state.compositor, compositor.display, state.renderer);
	state.wl_shell = wlr_wl_shell_create(compositor.display);
	state.xdg_shell = wlr_xdg_shell_v6_create(compositor.display);
	state.data_device_manager = wlr_data_device_manager_create(compositor.display);

	state.wl_seat = wlr_seat_create(compositor.display, "seat0");
	state.keyboard_bound.notify = handle_keyboard_bound;
	wl_signal_add(&state.wl_seat->events.keyboard_bound, &state.keyboard_bound);
	wlr_seat_set_capabilities(state.wl_seat, WL_SEAT_CAPABILITY_KEYBOARD
		| WL_SEAT_CAPABILITY_POINTER | WL_SEAT_CAPABILITY_TOUCH);

	struct keyboard_state *kbstate;
	wl_list_for_each(kbstate, &compositor.keyboards, link) {
		char *keymap = xkb_keymap_get_as_string(kbstate->keymap,
			XKB_KEYMAP_FORMAT_TEXT_V1);
		state.keymap_size = strlen(keymap);
		state.keymap_fd = os_create_anonymous_file(state.keymap_size);
		void *ptr = mmap(NULL, state.keymap_size,
				     PROT_READ | PROT_WRITE,
				     MAP_SHARED, state.keymap_fd, 0);
		strcpy(ptr, keymap);
		free(keymap);
		break;
	}

	wl_display_run(compositor.display);

	close(state.keymap_fd);
	wlr_seat_destroy(state.wl_seat);
	wlr_data_device_manager_destroy(state.data_device_manager);
	wlr_xdg_shell_v6_destroy(state.xdg_shell);
	wlr_wl_shell_destroy(state.wl_shell);
	wlr_compositor_finish(&state.compositor);
	wlr_renderer_destroy(state.renderer);
	compositor_fini(&compositor);
}
