#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include <assert.h>
#include <stdlib.h>
#include <wayland-server-protocol.h>
#include <wayland-server.h>
#include <wlr/types/wlr_surface.h>
#include <wlr/types/wlr_wl_shell.h>
#include <wlr/util/log.h>
#include "util/signal.h"

static const char *wlr_wl_shell_surface_role = "wl-shell-surface";

bool wlr_surface_is_wl_shell_surface(struct wlr_surface *surface) {
	return surface->role != NULL &&
		strcmp(surface->role, wlr_wl_shell_surface_role) == 0;
}

struct wlr_wl_surface *wlr_wl_shell_surface_from_wlr_surface(
		struct wlr_surface *surface) {
	assert(wlr_surface_is_wl_shell_surface(surface));
	return (struct wlr_wl_surface *)surface->role_data;
}

static void shell_pointer_grab_end(struct wlr_seat_pointer_grab *grab) {
	struct wlr_wl_shell_popup_grab *popup_grab = grab->data;

	struct wlr_wl_shell_surface *popup, *tmp = NULL;
	wl_list_for_each_safe(popup, tmp, &popup_grab->popups, grab_link) {
		if (popup->popup_mapped) {
			wl_shell_surface_send_popup_done(popup->resource);
			popup->popup_mapped = false;
		}
	}

	if (grab->seat->pointer_state.grab == grab) {
		wlr_seat_pointer_end_grab(grab->seat);
	}
}

static void shell_pointer_grab_maybe_end(struct wlr_seat_pointer_grab *grab) {
	struct wlr_wl_shell_popup_grab *popup_grab = grab->data;

	if (grab->seat->pointer_state.grab != grab) {
		return;
	}

	bool has_mapped = false;

	struct wlr_wl_shell_surface *popup, *tmp = NULL;
	wl_list_for_each_safe(popup, tmp, &popup_grab->popups, grab_link) {
		if (popup->popup_mapped) {
			has_mapped = true;
			break;
		}
	}

	if (!has_mapped) {
		shell_pointer_grab_end(grab);
	}
}

static void shell_pointer_grab_enter(struct wlr_seat_pointer_grab *grab,
		struct wlr_surface *surface, double sx, double sy) {
	struct wlr_wl_shell_popup_grab *popup_grab = grab->data;
	if (wl_resource_get_client(surface->resource) == popup_grab->client) {
		wlr_seat_pointer_enter(grab->seat, surface, sx, sy);
	} else {
		wlr_seat_pointer_clear_focus(grab->seat);
	}
}

static void shell_pointer_grab_motion(struct wlr_seat_pointer_grab *grab,
		uint32_t time, double sx, double sy) {
	wlr_seat_pointer_send_motion(grab->seat, time, sx, sy);
}

static uint32_t shell_pointer_grab_button(struct wlr_seat_pointer_grab *grab,
		uint32_t time, uint32_t button, uint32_t state) {
	uint32_t serial =
		wlr_seat_pointer_send_button(grab->seat, time, button, state);
	if (serial) {
		return serial;
	} else {
		shell_pointer_grab_end(grab);
		return 0;
	}
}

static void shell_pointer_grab_cancel(struct wlr_seat_pointer_grab *grab) {
	shell_pointer_grab_end(grab);
}

static void shell_pointer_grab_axis(struct wlr_seat_pointer_grab *grab,
		uint32_t time, enum wlr_axis_orientation orientation, double value,
		int32_t value_discrete, enum wlr_axis_source source) {
	wlr_seat_pointer_send_axis(grab->seat, time, orientation, value,
		value_discrete, source);
}

static const struct wlr_pointer_grab_interface shell_pointer_grab_impl = {
	.enter = shell_pointer_grab_enter,
	.motion = shell_pointer_grab_motion,
	.button = shell_pointer_grab_button,
	.cancel = shell_pointer_grab_cancel,
	.axis = shell_pointer_grab_axis,
};

static const struct wl_shell_surface_interface shell_surface_impl;

static struct wlr_wl_shell_surface *shell_surface_from_resource(
		struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource, &wl_shell_surface_interface,
		&shell_surface_impl));
	return wl_resource_get_user_data(resource);
}

static void shell_surface_protocol_pong(struct wl_client *client,
		struct wl_resource *resource, uint32_t serial) {
	wlr_log(WLR_DEBUG, "got shell surface pong");
	struct wlr_wl_shell_surface *surface = shell_surface_from_resource(resource);
	if (surface->ping_serial != serial) {
		return;
	}

	wl_event_source_timer_update(surface->ping_timer, 0);
	surface->ping_serial = 0;
}

static void shell_surface_protocol_move(struct wl_client *client,
		struct wl_resource *resource, struct wl_resource *seat_resource,
		uint32_t serial) {
	struct wlr_wl_shell_surface *surface = shell_surface_from_resource(resource);
	struct wlr_seat_client *seat = wlr_seat_client_from_resource(seat_resource);

	if (!wlr_seat_validate_grab_serial(seat->seat, serial)) {
		wlr_log(WLR_DEBUG, "invalid serial for grab");
		return;
	}

	struct wlr_wl_shell_surface_move_event event = {
		.surface = surface,
		.seat = seat,
		.serial = serial,
	};

	wlr_signal_emit_safe(&surface->events.request_move, &event);
}

static struct wlr_wl_shell_popup_grab *shell_popup_grab_from_seat(
		struct wlr_wl_shell *shell, struct wlr_seat *seat) {
	struct wlr_wl_shell_popup_grab *shell_grab;
	wl_list_for_each(shell_grab, &shell->popup_grabs, link) {
		if (shell_grab->seat == seat) {
			return shell_grab;
		}
	}

	shell_grab = calloc(1, sizeof(struct wlr_wl_shell_popup_grab));
	if (!shell_grab) {
		return NULL;
	}

	shell_grab->pointer_grab.data = shell_grab;
	shell_grab->pointer_grab.interface = &shell_pointer_grab_impl;

	wl_list_init(&shell_grab->popups);

	wl_list_insert(&shell->popup_grabs, &shell_grab->link);
	shell_grab->seat = seat;

	return shell_grab;
}

static void shell_surface_destroy_popup_state(
		struct wlr_wl_shell_surface *surface) {
	if (surface->popup_state) {
		wl_list_remove(&surface->grab_link);
		struct wlr_wl_shell_popup_grab *grab =
			shell_popup_grab_from_seat(surface->shell,
					surface->popup_state->seat);
		if (wl_list_empty(&grab->popups)) {
			if (grab->seat->pointer_state.grab == &grab->pointer_grab) {
				wlr_seat_pointer_end_grab(grab->seat);
			}
		}
		free(surface->popup_state);
		surface->popup_state = NULL;
	}
}

static void shell_surface_protocol_resize(struct wl_client *client,
		struct wl_resource *resource, struct wl_resource *seat_resource,
		uint32_t serial, enum wl_shell_surface_resize edges) {
	struct wlr_wl_shell_surface *surface = shell_surface_from_resource(resource);
	struct wlr_seat_client *seat = wlr_seat_client_from_resource(seat_resource);

	if (!wlr_seat_validate_grab_serial(seat->seat, serial)) {
		wlr_log(WLR_DEBUG, "invalid serial for grab");
		return;
	}

	struct wlr_wl_shell_surface_resize_event event = {
		.surface = surface,
		.seat = seat,
		.serial = serial,
		.edges = edges,
	};

	wlr_signal_emit_safe(&surface->events.request_resize, &event);
}

static void shell_surface_set_state(struct wlr_wl_shell_surface *surface,
		enum wlr_wl_shell_surface_state state,
		struct wlr_wl_shell_surface_transient_state *transient_state,
		struct wlr_wl_shell_surface_popup_state *popup_state) {
	surface->state = state;
	free(surface->transient_state);
	surface->transient_state = transient_state;
	shell_surface_destroy_popup_state(surface);
	surface->popup_state = popup_state;

	wlr_signal_emit_safe(&surface->events.set_state, surface);
}

static void shell_surface_protocol_set_toplevel(struct wl_client *client,
		struct wl_resource *resource) {
	wlr_log(WLR_DEBUG, "got shell surface toplevel");
	struct wlr_wl_shell_surface *surface = shell_surface_from_resource(resource);
	shell_surface_set_state(surface, WLR_WL_SHELL_SURFACE_STATE_TOPLEVEL, NULL,
		NULL);
}

static void shell_surface_popup_set_parent(struct wlr_wl_shell_surface *surface,
		struct wlr_wl_shell_surface *parent) {
	assert(surface && surface->state == WLR_WL_SHELL_SURFACE_STATE_POPUP);
	if (surface->parent == parent) {
		return;
	}
	surface->parent = parent;
	if (parent) {
		wl_list_remove(&surface->popup_link);
		wl_list_insert(&parent->popups, &surface->popup_link);
		wlr_signal_emit_safe(&parent->events.new_popup, surface);
	}
}

static struct wlr_wl_shell_surface *shell_find_shell_surface(
		struct wlr_wl_shell *shell, struct wlr_surface *surface) {
	if (surface) {
		struct wlr_wl_shell_surface *wl_surface;
		wl_list_for_each(wl_surface, &shell->surfaces, link) {
			if (wl_surface->surface == surface) {
				return wl_surface;
			}
		}
	}
	return NULL;
}

static void shell_surface_protocol_set_transient(struct wl_client *client,
		struct wl_resource *resource, struct wl_resource *parent_resource,
		int32_t x, int32_t y, enum wl_shell_surface_transient flags) {
	wlr_log(WLR_DEBUG, "got shell surface transient");
	struct wlr_wl_shell_surface *surface = shell_surface_from_resource(resource);
	struct wlr_surface *parent = wlr_surface_from_resource(parent_resource);
	// TODO: check if parent_resource == NULL?

	struct wlr_wl_shell_surface *wl_parent =
		shell_find_shell_surface(surface->shell, parent);

	if (!wl_parent) {
		return;
	}

	struct wlr_wl_shell_surface_transient_state *transient_state =
		calloc(1, sizeof(struct wlr_wl_shell_surface_transient_state));
	if (transient_state == NULL) {
		wl_client_post_no_memory(client);
		return;
	}

	surface->parent = wl_parent;
	transient_state->x = x;
	transient_state->y = y;
	transient_state->flags = flags;

	shell_surface_set_state(surface, WLR_WL_SHELL_SURFACE_STATE_TRANSIENT,
		transient_state, NULL);
}

static void shell_surface_protocol_set_fullscreen(struct wl_client *client,
		struct wl_resource *resource,
		enum wl_shell_surface_fullscreen_method method, uint32_t framerate,
		struct wl_resource *output_resource) {
	struct wlr_wl_shell_surface *surface = shell_surface_from_resource(resource);
	struct wlr_output *output = NULL;
	if (output_resource != NULL) {
		output = wlr_output_from_resource(output_resource);
	}

	shell_surface_set_state(surface, WLR_WL_SHELL_SURFACE_STATE_FULLSCREEN,
		NULL, NULL);

	struct wlr_wl_shell_surface_set_fullscreen_event event = {
		.surface = surface,
		.method = method,
		.framerate = framerate,
		.output = output,
	};

	wlr_signal_emit_safe(&surface->events.request_fullscreen, &event);
}

static void shell_surface_protocol_set_popup(struct wl_client *client,
		struct wl_resource *resource, struct wl_resource *seat_resource,
		uint32_t serial, struct wl_resource *parent_resource, int32_t x,
		int32_t y, enum wl_shell_surface_transient flags) {
	struct wlr_wl_shell_surface *surface = shell_surface_from_resource(resource);
	struct wlr_seat_client *seat_client =
		wlr_seat_client_from_resource(seat_resource);
	struct wlr_surface *parent = wlr_surface_from_resource(parent_resource);
	struct wlr_wl_shell_popup_grab *grab =
		shell_popup_grab_from_seat(surface->shell, seat_client->seat);
	if (!grab) {
		wl_client_post_no_memory(client);
		return;
	}

	struct wlr_wl_shell_surface *wl_parent =
		shell_find_shell_surface(surface->shell, parent);

	if (surface->state == WLR_WL_SHELL_SURFACE_STATE_POPUP) {
		surface->transient_state->x = x;
		surface->transient_state->y = y;
		shell_surface_popup_set_parent(surface, wl_parent);
		grab->client = surface->client;
		surface->popup_mapped = true;
		wlr_seat_pointer_start_grab(seat_client->seat, &grab->pointer_grab);
		return;
	}

	struct wlr_wl_shell_surface_transient_state *transient_state =
		calloc(1, sizeof(struct wlr_wl_shell_surface_transient_state));
	if (transient_state == NULL) {
		wl_client_post_no_memory(client);
		return;
	}
	transient_state->x = x;
	transient_state->y = y;
	transient_state->flags = flags;

	struct wlr_wl_shell_surface_popup_state *popup_state =
		calloc(1, sizeof(struct wlr_wl_shell_surface_popup_state));
	if (popup_state == NULL) {
		free(transient_state);
		wl_client_post_no_memory(client);
		return;
	}
	popup_state->seat = seat_client->seat;
	popup_state->serial = serial;

	shell_surface_set_state(surface, WLR_WL_SHELL_SURFACE_STATE_POPUP,
		transient_state, popup_state);

	shell_surface_popup_set_parent(surface, wl_parent);
	grab->client = surface->client;
	wl_list_insert(&grab->popups, &surface->grab_link);
	surface->popup_mapped = true;
	wlr_seat_pointer_start_grab(seat_client->seat, &grab->pointer_grab);
}

static void shell_surface_protocol_set_maximized(struct wl_client *client,
		struct wl_resource *resource, struct wl_resource *output_resource) {
	struct wlr_wl_shell_surface *surface = shell_surface_from_resource(resource);
	struct wlr_output *output = NULL;
	if (output_resource != NULL) {
		output = wlr_output_from_resource(output_resource);
	}

	shell_surface_set_state(surface, WLR_WL_SHELL_SURFACE_STATE_MAXIMIZED,
		NULL, NULL);

	struct wlr_wl_shell_surface_maximize_event event = {
		.surface = surface,
		.output = output,
	};

	wlr_signal_emit_safe(&surface->events.request_maximize, &event);
}

static void shell_surface_protocol_set_title(struct wl_client *client,
		struct wl_resource *resource, const char *title) {
	wlr_log(WLR_DEBUG, "new shell surface title: %s", title);
	struct wlr_wl_shell_surface *surface = shell_surface_from_resource(resource);

	char *tmp = strdup(title);
	if (tmp == NULL) {
		return;
	}

	free(surface->title);
	surface->title = tmp;

	wlr_signal_emit_safe(&surface->events.set_title, surface);
}

static void shell_surface_protocol_set_class(struct wl_client *client,
		struct wl_resource *resource, const char *class) {
	wlr_log(WLR_DEBUG, "new shell surface class: %s", class);
	struct wlr_wl_shell_surface *surface = shell_surface_from_resource(resource);

	char *tmp = strdup(class);
	if (tmp == NULL) {
		return;
	}

	free(surface->class);
	surface->class = tmp;

	wlr_signal_emit_safe(&surface->events.set_class, surface);
}

static const struct wl_shell_surface_interface shell_surface_impl = {
	.pong = shell_surface_protocol_pong,
	.move = shell_surface_protocol_move,
	.resize = shell_surface_protocol_resize,
	.set_toplevel = shell_surface_protocol_set_toplevel,
	.set_transient = shell_surface_protocol_set_transient,
	.set_fullscreen = shell_surface_protocol_set_fullscreen,
	.set_popup = shell_surface_protocol_set_popup,
	.set_maximized = shell_surface_protocol_set_maximized,
	.set_title = shell_surface_protocol_set_title,
	.set_class = shell_surface_protocol_set_class,
};

static void shell_surface_destroy(struct wlr_wl_shell_surface *surface) {
	wlr_signal_emit_safe(&surface->events.destroy, surface);
	shell_surface_destroy_popup_state(surface);
	wl_resource_set_user_data(surface->resource, NULL);

	struct wlr_wl_shell_surface *child;
	wl_list_for_each(child, &surface->popups, popup_link) {
		shell_surface_popup_set_parent(child, NULL);
	}
	wl_list_remove(&surface->popup_link);

	wl_list_remove(&surface->link);
	wl_list_remove(&surface->surface_destroy_listener.link);
	wlr_surface_set_role_committed(surface->surface, NULL, NULL);
	wl_event_source_remove(surface->ping_timer);
	free(surface->transient_state);
	free(surface->title);
	free(surface->class);
	free(surface);
}

static void shell_surface_resource_destroy(struct wl_resource *resource) {
	struct wlr_wl_shell_surface *surface = shell_surface_from_resource(resource);
	if (surface != NULL) {
		shell_surface_destroy(surface);
	}
}

static void shell_surface_handle_surface_destroy(struct wl_listener *listener,
		void *data) {
	struct wlr_wl_shell_surface *surface =
		wl_container_of(listener, surface, surface_destroy_listener);
	shell_surface_destroy(surface);
}
static void handle_surface_committed(struct wlr_surface *wlr_surface,
		void *role_data) {
	struct wlr_wl_shell_surface *surface = role_data;
	if (!surface->configured &&
			wlr_surface_has_buffer(surface->surface) &&
			surface->state != WLR_WL_SHELL_SURFACE_STATE_NONE) {
		surface->configured = true;
		wlr_signal_emit_safe(&surface->shell->events.new_surface, surface);
	}

	if (surface->popup_mapped &&
			surface->state == WLR_WL_SHELL_SURFACE_STATE_POPUP &&
			!wlr_surface_has_buffer(surface->surface)) {
		surface->popup_mapped = false;
		struct wlr_wl_shell_popup_grab *grab =
			shell_popup_grab_from_seat(surface->shell,
				surface->popup_state->seat);
		shell_pointer_grab_maybe_end(&grab->pointer_grab);
	}
}

static int shell_surface_ping_timeout(void *user_data) {
	struct wlr_wl_shell_surface *surface = user_data;
	wlr_signal_emit_safe(&surface->events.ping_timeout, surface);

	surface->ping_serial = 0;
	return 1;
}

static const struct wl_shell_interface shell_impl;

static struct wlr_wl_shell *shell_from_resource(
		struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource, &wl_shell_interface, &shell_impl));
	return wl_resource_get_user_data(resource);
}

static void shell_protocol_get_shell_surface(struct wl_client *client,
		struct wl_resource *shell_resource, uint32_t id,
		struct wl_resource *surface_resource) {
	struct wlr_surface *surface = wlr_surface_from_resource(surface_resource);
	if (wlr_surface_set_role(surface, wlr_wl_shell_surface_role,
			shell_resource, WL_SHELL_ERROR_ROLE)) {
		return;
	}

	struct wlr_wl_shell *wl_shell = shell_from_resource(shell_resource);
	struct wlr_wl_shell_surface *wl_surface =
		calloc(1, sizeof(struct wlr_wl_shell_surface));
	if (wl_surface == NULL) {
		wl_resource_post_no_memory(shell_resource);
		return;
	}
	wl_list_init(&wl_surface->grab_link);
	wl_list_init(&wl_surface->popup_link);
	wl_list_init(&wl_surface->popups);

	wl_surface->shell = wl_shell;
	wl_surface->client = client;
	wl_surface->surface = surface;

	wl_surface->resource = wl_resource_create(client,
		&wl_shell_surface_interface, wl_resource_get_version(shell_resource),
		id);
	if (wl_surface->resource == NULL) {
		free(wl_surface);
		wl_resource_post_no_memory(shell_resource);
		return;
	}
	wl_resource_set_implementation(wl_surface->resource,
		&shell_surface_impl, wl_surface,
		shell_surface_resource_destroy);

	wlr_log(WLR_DEBUG, "new wl_shell %p (res %p)", wl_surface,
		wl_surface->resource);

	wl_signal_init(&wl_surface->events.destroy);
	wl_signal_init(&wl_surface->events.ping_timeout);
	wl_signal_init(&wl_surface->events.new_popup);
	wl_signal_init(&wl_surface->events.request_move);
	wl_signal_init(&wl_surface->events.request_resize);
	wl_signal_init(&wl_surface->events.request_fullscreen);
	wl_signal_init(&wl_surface->events.request_maximize);
	wl_signal_init(&wl_surface->events.set_state);
	wl_signal_init(&wl_surface->events.set_title);
	wl_signal_init(&wl_surface->events.set_class);

	wl_signal_add(&wl_surface->surface->events.destroy,
		&wl_surface->surface_destroy_listener);
	wl_surface->surface_destroy_listener.notify =
		shell_surface_handle_surface_destroy;

	wlr_surface_set_role_committed(surface, handle_surface_committed,
		wl_surface);

	struct wl_display *display = wl_client_get_display(client);
	struct wl_event_loop *loop = wl_display_get_event_loop(display);
	wl_surface->ping_timer = wl_event_loop_add_timer(loop,
		shell_surface_ping_timeout, wl_surface);
	if (wl_surface->ping_timer == NULL) {
		wl_client_post_no_memory(client);
	}

	wl_list_insert(&wl_shell->surfaces, &wl_surface->link);
}

static const struct wl_shell_interface shell_impl = {
	.get_shell_surface = shell_protocol_get_shell_surface
};

static void shell_destroy(struct wl_resource *resource) {
	wl_list_remove(wl_resource_get_link(resource));
}

static void shell_bind(struct wl_client *wl_client, void *data,
		uint32_t version, uint32_t id) {
	struct wlr_wl_shell *wl_shell = data;
	assert(wl_client && wl_shell);

	struct wl_resource *wl_resource = wl_resource_create(wl_client,
		&wl_shell_interface, version, id);
	if (wl_resource == NULL) {
		wl_client_post_no_memory(wl_client);
		return;
	}
	wl_resource_set_implementation(wl_resource, &shell_impl, wl_shell,
		shell_destroy);
	wl_list_insert(&wl_shell->resources, wl_resource_get_link(wl_resource));
}

static void handle_display_destroy(struct wl_listener *listener, void *data) {
	struct wlr_wl_shell *wl_shell =
		wl_container_of(listener, wl_shell, display_destroy);
	wlr_wl_shell_destroy(wl_shell);
}

struct wlr_wl_shell *wlr_wl_shell_create(struct wl_display *display) {
	struct wlr_wl_shell *wl_shell = calloc(1, sizeof(struct wlr_wl_shell));
	if (!wl_shell) {
		return NULL;
	}
	wl_shell->ping_timeout = 10000;
	struct wl_global *global = wl_global_create(display, &wl_shell_interface,
		1, wl_shell, shell_bind);
	if (!global) {
		free(wl_shell);
		return NULL;
	}
	wl_shell->global = global;
	wl_list_init(&wl_shell->resources);
	wl_list_init(&wl_shell->surfaces);
	wl_list_init(&wl_shell->popup_grabs);
	wl_signal_init(&wl_shell->events.new_surface);

	wl_shell->display_destroy.notify = handle_display_destroy;
	wl_display_add_destroy_listener(display, &wl_shell->display_destroy);

	return wl_shell;
}

void wlr_wl_shell_destroy(struct wlr_wl_shell *wlr_wl_shell) {
	if (!wlr_wl_shell) {
		return;
	}
	wl_list_remove(&wlr_wl_shell->display_destroy.link);
	struct wl_resource *resource = NULL, *temp = NULL;
	wl_resource_for_each_safe(resource, temp, &wlr_wl_shell->resources) {
		// shell_destroy will remove the resource from the list
		wl_resource_destroy(resource);
	}
	// TODO: destroy surfaces
	wl_global_destroy(wlr_wl_shell->global);
	free(wlr_wl_shell);
}

void wlr_wl_shell_surface_ping(struct wlr_wl_shell_surface *surface) {
	if (surface->ping_serial != 0) {
		// already pinged
		return;
	}

	surface->ping_serial =
		wl_display_next_serial(wl_client_get_display(surface->client));
	wl_event_source_timer_update(surface->ping_timer,
		surface->shell->ping_timeout);
	wl_shell_surface_send_ping(surface->resource, surface->ping_serial);
}

void wlr_wl_shell_surface_configure(struct wlr_wl_shell_surface *surface,
		uint32_t edges, int32_t width, int32_t height) {
	wl_shell_surface_send_configure(surface->resource, edges, width, height);
}

struct wlr_surface *wlr_wl_shell_surface_surface_at(
		struct wlr_wl_shell_surface *surface, double sx, double sy,
		double *sub_sx, double *sub_sy) {
	struct wlr_wl_shell_surface *popup;
	wl_list_for_each(popup, &surface->popups, popup_link) {
		if (!popup->popup_mapped) {
			continue;
		}

		double popup_sx = popup->transient_state->x;
		double popup_sy = popup->transient_state->y;
		struct wlr_surface *sub = wlr_wl_shell_surface_surface_at(popup,
			sx - popup_sx, sy - popup_sy, sub_sx, sub_sy);
		if (sub != NULL) {
			return sub;
		}
	}

	return wlr_surface_surface_at(surface->surface, sx, sy, sub_sx, sub_sy);
}

struct wl_shell_surface_iterator_data {
	wlr_surface_iterator_func_t user_iterator;
	void *user_data;
	int x, y;
};

static void wl_shell_surface_iterator(struct wlr_surface *surface,
		int sx, int sy, void *data) {
	struct wl_shell_surface_iterator_data *iter_data = data;
	iter_data->user_iterator(surface, iter_data->x + sx, iter_data->y + sy,
		iter_data->user_data);
}

static void wl_shell_surface_for_each_surface(
		struct wlr_wl_shell_surface *surface, int x, int y,
		wlr_surface_iterator_func_t iterator, void *user_data) {
	struct wl_shell_surface_iterator_data data = {
		.user_iterator = iterator,
		.user_data = user_data,
		.x = x, .y = y,
	};
	wlr_surface_for_each_surface(surface->surface, wl_shell_surface_iterator,
		&data);

	struct wlr_wl_shell_surface *popup;
	wl_list_for_each(popup, &surface->popups, popup_link) {
		double popup_x = popup->transient_state->x;
		double popup_y = popup->transient_state->y;

		wl_shell_surface_for_each_surface(popup, x + popup_x, y + popup_y,
			iterator, user_data);
	}
}

void wlr_wl_shell_surface_for_each_surface(struct wlr_wl_shell_surface *surface,
		wlr_surface_iterator_func_t iterator, void *user_data) {
	wl_shell_surface_for_each_surface(surface, 0, 0, iterator, user_data);
}
