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
#include <wlr/types/wlr_gamma_control.h>
#include "wlr/types/wlr_compositor.h"
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/xwayland.h>
#include <xkbcommon/xkbcommon.h>
#include <wlr/util/log.h>
#include "config.h"
#include "shared.h"

// TODO: move to common header?
int os_create_anonymous_file(off_t size);

struct sample_state {
	struct wlr_renderer *renderer;
	struct compositor_state *compositor;
	struct wlr_compositor *wlr_compositor;
	struct wlr_wl_shell *wl_shell;
	struct wlr_seat *wl_seat;
	struct wlr_xdg_shell_v6 *xdg_shell;
	struct wlr_data_device_manager *data_device_manager;
	struct wl_resource *focus;
	struct wl_listener keyboard_bound;
	struct wlr_xwayland *xwayland;
	struct wlr_gamma_control_manager *gamma_control_manager;
	int keymap_fd;
	size_t keymap_size;
	uint32_t serial;

	struct example_config *config;
	struct wlr_output_layout *layout;
	struct wlr_cursor *cursor;
	struct wlr_xcursor *xcursor;

	struct wl_listener cursor_motion;
	struct wl_listener cursor_motion_absolute;
	struct wl_listener cursor_button;
	struct wl_listener cursor_axis;

	struct wl_listener new_xdg_surface_v6;
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

static void handle_new_xdg_surface_v6(struct wl_listener *listener,
		void *data) {
	struct wlr_xdg_surface_v6 *surface = data;
	wlr_log(L_DEBUG, "new xdg surface: title=%s, app_id=%s",
		surface->title, surface->app_id);
	// configure the surface and add it to data structures here
}

static void handle_output_frame(struct output_state *output,
		struct timespec *ts) {
	struct compositor_state *state = output->compositor;
	struct sample_state *sample = state->data;
	struct wlr_output *wlr_output = output->output;

	wlr_output_make_current(wlr_output);
	wlr_renderer_begin(sample->renderer, wlr_output);

	struct wlr_wl_shell_surface *wl_shell_surface;
	wl_list_for_each(wl_shell_surface, &sample->wl_shell->surfaces, link) {
		output_frame_handle_surface(sample, wlr_output, ts,
			wl_shell_surface->surface);
	}
	struct wlr_xdg_surface_v6 *xdg_surface;
	wl_list_for_each(xdg_surface, &sample->xdg_shell->surfaces, link) {
		output_frame_handle_surface(sample, wlr_output, ts,
			xdg_surface->surface->resource);
	}
	struct wlr_x11_window *x11_window;
	wl_list_for_each(x11_window, &sample->xwayland->displayable_windows, link) {
		output_frame_handle_surface(sample, wlr_output, ts,
			x11_window->surface);
	}

	wlr_renderer_end(sample->renderer);
	wlr_output_swap_buffers(wlr_output);
}

static void handle_keyboard_key(struct keyboard_state *keyboard,
		uint32_t keycode, xkb_keysym_t sym, enum wlr_key_state key_state) {
	struct compositor_state *state = keyboard->compositor;
	struct sample_state *sample = state->data;

	struct wl_resource *res = NULL;
	struct wlr_seat_handle *seat_handle = NULL;
	wl_list_for_each(res, &sample->wlr_compositor->surfaces, link) {
		break;
	}

	if (res) {
		seat_handle = wlr_seat_handle_for_client(sample->wl_seat,
			wl_resource_get_client(res));
	}

	if (res != sample->focus && seat_handle && seat_handle->keyboard) {
		struct wl_array keys;
		wl_array_init(&keys);
		wl_keyboard_send_enter(seat_handle->keyboard, ++sample->serial, res,
			&keys);
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
		wl_keyboard_send_modifiers(seat_handle->keyboard, ++sample->serial,
			depressed, latched, locked, group);
		wl_keyboard_send_key(seat_handle->keyboard, ++sample->serial, 0,
			keycode, key_state);
	}
}

static void handle_keyboard_bound(struct wl_listener *listener, void *data) {
	struct wlr_seat_handle *handle = data;
	struct sample_state *state =
		wl_container_of(listener, state, keyboard_bound);

	wl_keyboard_send_keymap(handle->keyboard, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1,
		state->keymap_fd, state->keymap_size);

	if (wl_resource_get_version(handle->keyboard) >= 2) {
		wl_keyboard_send_repeat_info(handle->keyboard, 25, 600);
	}
}

static void handle_cursor_motion(struct wl_listener *listener, void *data) {
	struct sample_state *sample =
		wl_container_of(listener, sample, cursor_motion);
	struct wlr_event_pointer_motion *event = data;
	wlr_cursor_move(sample->cursor, event->device, event->delta_x,
		event->delta_y);
}

static void handle_cursor_motion_absolute(struct wl_listener *listener,
		void *data) {
	struct sample_state *sample =
		wl_container_of(listener, sample, cursor_motion_absolute);
	struct wlr_event_pointer_motion_absolute *event = data;

	wlr_cursor_warp_absolute(sample->cursor, event->device, event->x_mm,
		event->y_mm);
}

static void handle_cursor_axis(struct wl_listener *listener, void *data) {
	//struct sample_state *sample =
	//wl_container_of(listener, sample, cursor_axis);
	//struct wlr_event_pointer_axis *event = data;
	wlr_log(L_DEBUG, "TODO: handle cursor axis");
}

static void handle_cursor_button(struct wl_listener *listener, void *data) {
	//struct sample_state *sample =
	//wl_container_of(listener, sample, cursor_button);
	//struct wlr_event_pointer_button *event = data;
	wlr_log(L_DEBUG, "TODO: handle cursor button");
}

static void handle_input_add(struct compositor_state *state,
		struct wlr_input_device *device) {
	struct sample_state *sample = state->data;

	if (device->type == WLR_INPUT_DEVICE_POINTER ||
			device->type == WLR_INPUT_DEVICE_TOUCH ||
			device->type == WLR_INPUT_DEVICE_TABLET_TOOL) {
		wlr_cursor_attach_input_device(sample->cursor, device);
		example_config_configure_cursor(sample->config, sample->cursor,
			sample->compositor);
	}
}

static void handle_output_add(struct output_state *ostate) {
	struct sample_state *sample = ostate->compositor->data;
	struct wlr_output *wlr_output = ostate->output;
	struct wlr_xcursor_image *image = sample->xcursor->images[0];

	struct output_config *o_config =
		example_config_get_output(sample->config, ostate->output);

	if (o_config) {
		wlr_output_transform(ostate->output, o_config->transform);
		wlr_output_layout_add(sample->layout, ostate->output, o_config->x,
			o_config->y);
	} else {
		wlr_output_layout_add_auto(sample->layout, ostate->output);
	}

	example_config_configure_cursor(sample->config, sample->cursor,
		sample->compositor);

	// TODO the cursor must be set depending on which surface it is displayed
	// over which should happen in the compositor.
	if (!wlr_output_set_cursor(wlr_output, image->buffer,
			image->width, image->width, image->height)) {
		wlr_log(L_DEBUG, "Failed to set hardware cursor");
		return;
	}

	wlr_cursor_warp(sample->cursor, NULL, sample->cursor->x, sample->cursor->y);
}

static void handle_output_remove(struct output_state *ostate) {
	struct sample_state *sample = ostate->compositor->data;

	wlr_output_layout_remove(sample->layout, ostate->output);

	example_config_configure_cursor(sample->config, sample->cursor,
		sample->compositor);
}

int main(int argc, char *argv[]) {
	struct sample_state state = { 0 };
	struct compositor_state compositor = { 0,
		.data = &state,
		.output_frame_cb = handle_output_frame,
	};

	compositor.input_add_cb = handle_input_add;
	compositor.output_add_cb = handle_output_add;
	compositor.output_remove_cb = handle_output_remove;

	state.compositor = &compositor;
	state.config = parse_args(argc, argv);
	state.cursor = wlr_cursor_create();
	state.layout = wlr_output_layout_create();
	wlr_cursor_attach_output_layout(state.cursor, state.layout);
	wlr_cursor_map_to_region(state.cursor, state.config->cursor.mapped_box);

	struct wlr_xcursor_theme *theme = wlr_xcursor_theme_load("default", 16);
	if (!theme) {
		wlr_log(L_ERROR, "Failed to load cursor theme");
		return 1;
	}
	state.xcursor = wlr_xcursor_theme_get_cursor(theme, "left_ptr");
	if (!state.xcursor) {
		wlr_log(L_ERROR, "Failed to load left_ptr cursor");
		return 1;
	}

	wlr_cursor_set_xcursor(state.cursor, state.xcursor);

	// pointer events
	wl_signal_add(&state.cursor->events.motion, &state.cursor_motion);
	state.cursor_motion.notify = handle_cursor_motion;

	wl_signal_add(&state.cursor->events.motion_absolute,
		&state.cursor_motion_absolute);
	state.cursor_motion_absolute.notify = handle_cursor_motion_absolute;

	wl_signal_add(&state.cursor->events.button, &state.cursor_button);
	state.cursor_button.notify = handle_cursor_button;

	wl_signal_add(&state.cursor->events.axis, &state.cursor_axis);
	state.cursor_axis.notify = handle_cursor_axis;

	compositor_init(&compositor);

	state.renderer = wlr_gles2_renderer_create(compositor.backend);
	if (!state.renderer) {
		wlr_log(L_ERROR, "Could not start compositor, OOM");
		exit(EXIT_FAILURE);
	}
	wl_display_init_shm(compositor.display);
	state.wlr_compositor =
		wlr_compositor_create(compositor.display, state.renderer);
	state.wl_shell = wlr_wl_shell_create(compositor.display);
	state.xdg_shell = wlr_xdg_shell_v6_create(compositor.display);

	// shell events
	wl_signal_add(&state.xdg_shell->events.new_surface,
		&state.new_xdg_surface_v6);
	state.new_xdg_surface_v6.notify = handle_new_xdg_surface_v6;


	state.data_device_manager =
		wlr_data_device_manager_create(compositor.display);

	state.gamma_control_manager =
		wlr_gamma_control_manager_create(compositor.display);

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
		void *ptr =
			mmap(NULL, state.keymap_size, PROT_READ | PROT_WRITE, MAP_SHARED,
				state.keymap_fd, 0);
		strcpy(ptr, keymap);
		free(keymap);
		break;
	}
	state.xwayland = wlr_xwayland_create(compositor.display,
		state.wlr_compositor);

	compositor.keyboard_key_cb = handle_keyboard_key;

	wl_display_run(compositor.display);

	wl_list_remove(&state.new_xdg_surface_v6.link);

	wlr_xwayland_destroy(state.xwayland);
	close(state.keymap_fd);
	wlr_seat_destroy(state.wl_seat);
	wlr_gamma_control_manager_destroy(state.gamma_control_manager);
	wlr_data_device_manager_destroy(state.data_device_manager);
	wlr_xdg_shell_v6_destroy(state.xdg_shell);
	wlr_wl_shell_destroy(state.wl_shell);
	wlr_compositor_destroy(state.wlr_compositor);
	wlr_renderer_destroy(state.renderer);
	compositor_fini(&compositor);
}
