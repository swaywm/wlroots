#define _POSIX_C_SOURCE 199309L
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <inttypes.h>
#include <unistd.h>
#include <sys/mman.h>
#include <wayland-server.h>
// TODO: BSD et al
#include <linux/input-event-codes.h>
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
#include <wlr/types/wlr_input_device.h>
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
#include <assert.h>

struct sample_state;

struct example_xdg_surface_v6 {
	struct wlr_xdg_surface_v6 *surface;
	struct sample_state *sample;

	// position of the wlr_surface in the layout
	struct {
		int lx;
		int ly;
	} position;

	struct wl_listener destroy_listener;
	struct wl_listener ping_timeout_listener;
	struct wl_listener request_minimize_listener;
	struct wl_listener request_move_listener;
	struct wl_listener request_resize_listener;
	struct wl_listener request_show_window_menu_listener;
};

struct input_event_cache {
	uint32_t serial;
	struct wlr_cursor *cursor;
	struct wlr_input_device *device;
};

struct motion_context {
	struct example_xdg_surface_v6 *surface;
	int off_x, off_y;
};

struct sample_state {
	struct wlr_renderer *renderer;
	struct compositor_state *compositor;
	struct wlr_compositor *wlr_compositor;
	struct wlr_wl_shell *wl_shell;
	struct wlr_seat *wl_seat;
	struct wlr_xdg_shell_v6 *xdg_shell;
	struct wlr_data_device_manager *data_device_manager;
	struct wl_resource *focus;
	struct wlr_xwayland *xwayland;
	struct wlr_gamma_control_manager *gamma_control_manager;
	bool mod_down;

	struct motion_context motion_context;

	struct example_config *config;
	struct wlr_output_layout *layout;
	struct wlr_cursor *cursor;
	struct wlr_xcursor *xcursor;

	// Ring buffer
	int input_cache_idx;
	struct input_event_cache input_cache[16];

	struct wl_listener cursor_motion;
	struct wl_listener cursor_motion_absolute;
	struct wl_listener cursor_button;
	struct wl_listener cursor_axis;

	struct wl_listener tool_axis;
	struct wl_listener tool_tip;
	struct wl_listener tool_button;

	struct wl_listener new_xdg_surface_v6;

	struct wlr_xdg_surface_v6 *focused_surface;
};

static void example_set_focused_surface(struct sample_state *sample,
		struct wlr_xdg_surface_v6 *surface) {
	if (sample->focused_surface == surface) {
		return;
	}

	// set activated state of the xdg surfaces
	struct wlr_xdg_surface_v6 *xdg_surface;
	struct wlr_xdg_client_v6 *xdg_client;
	wl_list_for_each(xdg_client, &sample->xdg_shell->clients, link) {
		wl_list_for_each(xdg_surface, &xdg_client->surfaces, link) {
			if (!xdg_surface->configured ||
					xdg_surface->role != WLR_XDG_SURFACE_V6_ROLE_TOPLEVEL) {
				continue;
			}
			wlr_xdg_toplevel_v6_set_activated(xdg_surface,
				xdg_surface == surface);
		}
	}

	if (surface) {
		wlr_seat_keyboard_enter(sample->wl_seat, surface->surface);
	} else {
		wlr_seat_keyboard_clear_focus(sample->wl_seat);
	}

	sample->focused_surface = surface;
}

/*
 * Convert timespec to milliseconds
 */
static inline int64_t timespec_to_msec(const struct timespec *a) {
	return (int64_t)a->tv_sec * 1000 + a->tv_nsec / 1000000;
}

static void output_frame_handle_surface(struct sample_state *sample,
		struct wlr_output *wlr_output, struct timespec *ts,
		struct wl_resource *_res, int ox, int oy) {
	struct wlr_surface *surface = wl_resource_get_user_data(_res);
	float matrix[16];
	float transform[16];
	wlr_surface_flush_damage(surface);
	if (surface->texture->valid) {
		wlr_matrix_translate(&transform, ox, oy, 0);
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

static void handle_xdg_surface_v6_ping_timeout(struct wl_listener *listener,
		void *data) {
	struct wlr_xdg_surface_v6 *surface = data;
	wlr_log(L_DEBUG, "got ping timeout for surface: %s", surface->title);
}

static void handle_xdg_surface_v6_destroy(struct wl_listener *listener,
		void *data) {
	struct example_xdg_surface_v6 *example_surface =
		wl_container_of(listener, example_surface, destroy_listener);
	wl_list_remove(&example_surface->destroy_listener.link);
	wl_list_remove(&example_surface->ping_timeout_listener.link);
	wl_list_remove(&example_surface->request_move_listener.link);
	wl_list_remove(&example_surface->request_resize_listener.link);
	wl_list_remove(&example_surface->request_show_window_menu_listener.link);
	wl_list_remove(&example_surface->request_minimize_listener.link);
	free(example_surface);
}

static void handle_xdg_surface_v6_request_move(struct wl_listener *listener,
		void *data) {
	struct example_xdg_surface_v6 *esurface =
		wl_container_of(listener, esurface, request_move_listener);
	struct wlr_xdg_toplevel_v6_move_event *e = data;
	struct sample_state *sample = esurface->sample;
	struct input_event_cache *event = NULL;
	for (size_t i = 0;
			i < sizeof(sample->input_cache) / sizeof(sample->input_cache[0]);
			++i) {
		if (sample->input_cache[i].cursor
				&& sample->input_cache[i].serial == e->serial) {
			event = &sample->input_cache[i];
			break;
		}
	}
	if (!event || sample->motion_context.surface) {
		return;
	}
	sample->motion_context.surface = esurface;
	sample->motion_context.off_x = sample->cursor->x - esurface->position.lx;
	sample->motion_context.off_y = sample->cursor->y - esurface->position.ly;
	wlr_seat_pointer_clear_focus(sample->wl_seat);
}

static void handle_xdg_surface_v6_request_resize(struct wl_listener *listener,
		void *data) {
	struct example_xdg_surface_v6 *example_surface =
		wl_container_of(listener, example_surface, request_resize_listener);
	struct wlr_xdg_toplevel_v6_resize_event *e = data;
	wlr_log(L_DEBUG, "TODO: surface requested resize: %s", e->surface->title);
}

static void handle_xdg_surface_v6_request_show_window_menu(
		struct wl_listener *listener, void *data) {
	struct example_xdg_surface_v6 *example_surface =
		wl_container_of(listener, example_surface,
			request_show_window_menu_listener);
	struct wlr_xdg_toplevel_v6_show_window_menu_event *e = data;
	wlr_log(L_DEBUG, "TODO: surface requested to show window menu: %s",
		e->surface->title);
}

static void handle_xdg_surface_v6_request_minimize(
		struct wl_listener *listener, void *data) {
	struct example_xdg_surface_v6 *example_surface =
		wl_container_of(listener, example_surface, request_minimize_listener);
	wlr_log(L_DEBUG, "TODO: surface requested to be minimized: %s",
		example_surface->surface->title);
}

static void handle_new_xdg_surface_v6(struct wl_listener *listener,
		void *data) {
	struct sample_state *sample_state =
		wl_container_of(listener, sample_state, new_xdg_surface_v6);
	struct wlr_xdg_surface_v6 *surface = data;
	wlr_log(L_DEBUG, "new xdg surface: title=%s, app_id=%s",
		surface->title, surface->app_id);

	wlr_xdg_surface_v6_ping(surface);

	struct example_xdg_surface_v6 *esurface =
		calloc(1, sizeof(struct example_xdg_surface_v6));
	if (esurface == NULL) {
		return;
	}

	esurface->sample = sample_state;
	esurface->surface = surface;
	// TODO sensible default position
	esurface->position.lx = 300;
	esurface->position.ly = 300;
	surface->data = esurface;

	wl_signal_add(&surface->events.destroy, &esurface->destroy_listener);
	esurface->destroy_listener.notify = handle_xdg_surface_v6_destroy;

	wl_signal_add(&surface->events.ping_timeout,
		&esurface->ping_timeout_listener);
	esurface->ping_timeout_listener.notify = handle_xdg_surface_v6_ping_timeout;

	wl_signal_add(&surface->events.request_move,
		&esurface->request_move_listener);
	esurface->request_move_listener.notify = handle_xdg_surface_v6_request_move;

	wl_signal_add(&surface->events.request_resize,
		&esurface->request_resize_listener);
	esurface->request_resize_listener.notify =
		handle_xdg_surface_v6_request_resize;

	wl_signal_add(&surface->events.request_show_window_menu,
		&esurface->request_show_window_menu_listener);
	esurface->request_show_window_menu_listener.notify =
		handle_xdg_surface_v6_request_show_window_menu;

	wl_signal_add(&surface->events.request_minimize,
		&esurface->request_minimize_listener);
	esurface->request_minimize_listener.notify =
		handle_xdg_surface_v6_request_minimize;
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
			wl_shell_surface->surface->resource, 200, 200);
	}
	struct wlr_xdg_surface_v6 *xdg_surface;
	struct wlr_xdg_client_v6 *xdg_client;
	wl_list_for_each(xdg_client, &sample->xdg_shell->clients, link) {
		wl_list_for_each(xdg_surface, &xdg_client->surfaces, link) {
			if (!xdg_surface->configured) {
				continue;
			}

			struct example_xdg_surface_v6 *esurface = xdg_surface->data;
			assert(esurface);

			int width = xdg_surface->surface->current.buffer_width;
			int height = xdg_surface->surface->current.buffer_height;

			bool intersects_output = wlr_output_layout_intersects(
				sample->layout, wlr_output,
				esurface->position.lx, esurface->position.ly,
				esurface->position.lx + width, esurface->position.ly + height);

			if (intersects_output) {
				double ox = esurface->position.lx, oy = esurface->position.ly;
				wlr_output_layout_output_coords(sample->layout, wlr_output,
					&ox, &oy);
				output_frame_handle_surface(sample, wlr_output, ts,
					xdg_surface->surface->resource, ox, oy);
			}
		}
	}
	struct wlr_x11_window *x11_window;
	wl_list_for_each(x11_window, &sample->xwayland->displayable_windows, link) {
		output_frame_handle_surface(sample, wlr_output, ts,
			x11_window->surface, 200, 200);
	}

	wlr_renderer_end(sample->renderer);
	wlr_output_swap_buffers(wlr_output);
}

static void handle_keyboard_key(struct keyboard_state *keyboard,
		uint32_t keycode, xkb_keysym_t sym, enum wlr_key_state key_state,
		uint64_t time_usec) {
	struct compositor_state *state = keyboard->compositor;
	struct sample_state *sample = state->data;
	if (sym == XKB_KEY_Super_L || sym == XKB_KEY_Super_R) {
		sample->mod_down = key_state == WLR_KEY_PRESSED;
	}
}

static struct wlr_xdg_surface_v6 *example_xdg_surface_at(
		struct sample_state *sample, int lx, int ly) {
	struct wlr_xdg_surface_v6 *xdg_surface;
	struct wlr_xdg_client_v6 *xdg_client;
	wl_list_for_each(xdg_client, &sample->xdg_shell->clients, link) {
		wl_list_for_each(xdg_surface, &xdg_client->surfaces, link) {
			if (!xdg_surface->configured) {
				continue;
			}

			struct example_xdg_surface_v6 *esurface = xdg_surface->data;

			double window_x = esurface->position.lx + xdg_surface->geometry->x;
			double window_y = esurface->position.ly + xdg_surface->geometry->y;

			if (sample->cursor->x >= window_x &&
					sample->cursor->y >= window_y &&
					sample->cursor->x <= window_x +
						xdg_surface->geometry->width &&
					sample->cursor->y <= window_y +
						xdg_surface->geometry->height) {
				return xdg_surface;
			}
		}
	}

	return NULL;
}

static void update_pointer_position(struct sample_state *sample, uint32_t time) {
	if (sample->motion_context.surface) {
		struct example_xdg_surface_v6 *surface;
		surface = sample->motion_context.surface;
		surface->position.lx = sample->cursor->x - sample->motion_context.off_x;
		surface->position.ly = sample->cursor->y - sample->motion_context.off_y;
		return;
	}

	struct wlr_xdg_surface_v6 *surface = example_xdg_surface_at(sample,
			sample->cursor->x, sample->cursor->y);

	if (surface) {
		struct example_xdg_surface_v6 *esurface = surface->data;

		double sx = sample->cursor->x - esurface->position.lx;
		double sy = sample->cursor->y - esurface->position.ly;

		// TODO z-order
		wlr_seat_pointer_enter(sample->wl_seat, surface->surface, sx, sy);
		wlr_seat_pointer_send_motion(sample->wl_seat, time, sx, sy);
	} else {
		wlr_seat_pointer_clear_focus(sample->wl_seat);
	}
}

static void handle_cursor_motion(struct wl_listener *listener, void *data) {
	struct sample_state *sample =
		wl_container_of(listener, sample, cursor_motion);
	struct wlr_event_pointer_motion *event = data;

	wlr_cursor_move(sample->cursor, event->device, event->delta_x,
		event->delta_y);

	update_pointer_position(sample, (uint32_t)event->time_usec);
}

static void handle_cursor_motion_absolute(struct wl_listener *listener,
		void *data) {
	struct sample_state *sample =
		wl_container_of(listener, sample, cursor_motion_absolute);
	struct wlr_event_pointer_motion_absolute *event = data;

	wlr_cursor_warp_absolute(sample->cursor, event->device,
		event->x_mm / event->width_mm, event->y_mm / event->height_mm);

	update_pointer_position(sample, (uint32_t)event->time_usec);
}

static void handle_cursor_axis(struct wl_listener *listener, void *data) {
	struct sample_state *sample =
		wl_container_of(listener, sample, cursor_axis);
	struct wlr_event_pointer_axis *event = data;
	wlr_seat_pointer_send_axis(sample->wl_seat, event->time_sec,
		event->orientation, event->delta);
}

static void handle_cursor_button(struct wl_listener *listener, void *data) {
	struct sample_state *sample =
		wl_container_of(listener, sample, cursor_button);
	struct wlr_event_pointer_button *event = data;

	struct wlr_xdg_surface_v6 *surface =
		example_xdg_surface_at(sample, sample->cursor->x, sample->cursor->y);

	uint32_t serial = wlr_seat_pointer_send_button(sample->wl_seat,
			(uint32_t)event->time_usec, event->button, event->state);

	int i;
	switch (event->state) {
	case WLR_BUTTON_RELEASED:
		if (sample->motion_context.surface) {
			sample->motion_context.surface = NULL;
		}
		break;
	case WLR_BUTTON_PRESSED:
		i = sample->input_cache_idx;
		sample->input_cache[i].serial = serial;
		sample->input_cache[i].cursor = sample->cursor;
		sample->input_cache[i].device = event->device;
		sample->input_cache_idx = (i + 1)
			% (sizeof(sample->input_cache) / sizeof(sample->input_cache[0]));
		example_set_focused_surface(sample, surface);
		wlr_log(L_DEBUG, "Stored event %d at %d", serial, i);
		if (sample->mod_down && event->button == BTN_LEFT) {
			struct example_xdg_surface_v6 *esurface = surface->data;
			sample->motion_context.surface = esurface;
			sample->motion_context.off_x = sample->cursor->x - esurface->position.lx;
			sample->motion_context.off_y = sample->cursor->y - esurface->position.ly;
			wlr_seat_pointer_clear_focus(sample->wl_seat);
		}
		break;
	}
}

static void handle_tool_axis(struct wl_listener *listener, void *data) {
	struct sample_state *sample =
		wl_container_of(listener, sample, tool_axis);
	struct wlr_event_tablet_tool_axis *event = data;
	if ((event->updated_axes & WLR_TABLET_TOOL_AXIS_X) &&
			(event->updated_axes & WLR_TABLET_TOOL_AXIS_Y)) {
		wlr_cursor_warp_absolute(sample->cursor, event->device,
			event->x_mm / event->width_mm, event->y_mm / event->height_mm);
		update_pointer_position(sample, (uint32_t)event->time_usec);
	}
}

static void handle_tool_tip(struct wl_listener *listener, void *data) {
	struct sample_state *sample =
		wl_container_of(listener, sample, tool_tip);
	struct wlr_event_tablet_tool_tip *event = data;

	struct wlr_xdg_surface_v6 *surface =
		example_xdg_surface_at(sample, sample->cursor->x, sample->cursor->y);
	example_set_focused_surface(sample, surface);

	wlr_seat_pointer_send_button(sample->wl_seat, (uint32_t)event->time_usec,
		BTN_LEFT, event->state);
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

	if (device->type == WLR_INPUT_DEVICE_KEYBOARD) {
		struct xkb_rule_names rules;
		memset(&rules, 0, sizeof(rules));
		rules.rules = getenv("XKB_DEFAULT_RULES");
		rules.model = getenv("XKB_DEFAULT_MODEL");
		rules.layout = getenv("XKB_DEFAULT_LAYOUT");
		rules.variant = getenv("XKB_DEFAULT_VARIANT");
		rules.options = getenv("XKB_DEFAULT_OPTIONS");
		struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
		if (!context) {
			wlr_log(L_ERROR, "Failed to create XKB context");
			exit(1);
		}
		wlr_keyboard_set_keymap(device->keyboard, xkb_map_new_from_names(
				context, &rules, XKB_KEYMAP_COMPILE_NO_FLAGS));
		xkb_context_unref(context);
		wlr_seat_attach_keyboard(sample->wl_seat, device);
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

	wl_signal_add(&state.cursor->events.tablet_tool_axis, &state.tool_axis);
	state.tool_axis.notify = handle_tool_axis;

	wl_signal_add(&state.cursor->events.tablet_tool_tip, &state.tool_tip);
	state.tool_tip.notify = handle_tool_tip;

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
	assert(state.wl_seat);
	wlr_seat_set_capabilities(state.wl_seat, WL_SEAT_CAPABILITY_KEYBOARD
		| WL_SEAT_CAPABILITY_POINTER | WL_SEAT_CAPABILITY_TOUCH);

	state.xwayland = wlr_xwayland_create(compositor.display,
		state.wlr_compositor);

	compositor.keyboard_key_cb = handle_keyboard_key;

	if (!wlr_backend_start(compositor.backend)) {
		wlr_log(L_ERROR, "Failed to start backend");
		wlr_backend_destroy(compositor.backend);
		exit(1);
	}

	wl_display_run(compositor.display);

	wl_list_remove(&state.new_xdg_surface_v6.link);

	wlr_xwayland_destroy(state.xwayland);
	wlr_seat_destroy(state.wl_seat);
	wlr_gamma_control_manager_destroy(state.gamma_control_manager);
	wlr_data_device_manager_destroy(state.data_device_manager);
	wlr_xdg_shell_v6_destroy(state.xdg_shell);
	wlr_wl_shell_destroy(state.wl_shell);
	wlr_compositor_destroy(state.wlr_compositor);
	wlr_renderer_destroy(state.renderer);
	compositor_fini(&compositor);
}
