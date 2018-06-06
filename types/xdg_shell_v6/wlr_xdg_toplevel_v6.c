#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <wlr/util/log.h>
#include "types/wlr_xdg_shell_v6.h"
#include "util/signal.h"

static const struct zxdg_toplevel_v6_interface zxdg_toplevel_v6_implementation;

static struct wlr_xdg_surface_v6 *xdg_surface_from_xdg_toplevel_resource(
		struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource, &zxdg_toplevel_v6_interface,
		&zxdg_toplevel_v6_implementation));
	return wl_resource_get_user_data(resource);
}

void destroy_xdg_toplevel_v6(struct wlr_xdg_surface_v6 *surface) {
	assert(surface->role == WLR_XDG_SURFACE_V6_ROLE_TOPLEVEL);
	unmap_xdg_surface_v6(surface);

	wl_resource_set_user_data(surface->toplevel->resource, NULL);
	free(surface->toplevel);
	surface->toplevel = NULL;

	surface->role = WLR_XDG_SURFACE_V6_ROLE_NONE;
}

static void xdg_toplevel_handle_destroy(struct wl_client *client,
		struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static void xdg_toplevel_handle_set_parent(struct wl_client *client,
		struct wl_resource *resource, struct wl_resource *parent_resource) {
	struct wlr_xdg_surface_v6 *surface =
		xdg_surface_from_xdg_toplevel_resource(resource);

	struct wlr_xdg_surface_v6 *parent = NULL;
	if (parent_resource != NULL) {
		parent = xdg_surface_from_xdg_toplevel_resource(parent_resource);
	}

	surface->toplevel->parent = parent;
	wlr_signal_emit_safe(&surface->toplevel->events.set_parent, surface);
}

static void xdg_toplevel_handle_set_title(struct wl_client *client,
		struct wl_resource *resource, const char *title) {
	struct wlr_xdg_surface_v6 *surface =
		xdg_surface_from_xdg_toplevel_resource(resource);

	char *tmp = strdup(title);
	if (tmp == NULL) {
		return;
	}

	free(surface->toplevel->title);
	surface->toplevel->title = tmp;
}

static void xdg_toplevel_handle_set_app_id(struct wl_client *client,
		struct wl_resource *resource, const char *app_id) {
	struct wlr_xdg_surface_v6 *surface =
		xdg_surface_from_xdg_toplevel_resource(resource);

	char *tmp = strdup(app_id);
	if (tmp == NULL) {
		return;
	}

	free(surface->toplevel->app_id);
	surface->toplevel->app_id = tmp;
}

static void xdg_toplevel_handle_show_window_menu(struct wl_client *client,
		struct wl_resource *resource, struct wl_resource *seat_resource,
		uint32_t serial, int32_t x, int32_t y) {
	struct wlr_xdg_surface_v6 *surface =
		xdg_surface_from_xdg_toplevel_resource(resource);
	struct wlr_seat_client *seat =
		wlr_seat_client_from_resource(seat_resource);

	if (!surface->configured) {
		wl_resource_post_error(surface->toplevel->resource,
			ZXDG_SURFACE_V6_ERROR_NOT_CONSTRUCTED,
			"surface has not been configured yet");
		return;
	}

	if (!wlr_seat_validate_grab_serial(seat->seat, serial)) {
		wlr_log(L_DEBUG, "invalid serial for grab");
		return;
	}

	struct wlr_xdg_toplevel_v6_show_window_menu_event event = {
		.surface = surface,
		.seat = seat,
		.serial = serial,
		.x = x,
		.y = y,
	};

	wlr_signal_emit_safe(&surface->toplevel->events.request_show_window_menu,
		&event);
}

static void xdg_toplevel_handle_move(struct wl_client *client,
		struct wl_resource *resource, struct wl_resource *seat_resource,
		uint32_t serial) {
	struct wlr_xdg_surface_v6 *surface =
		xdg_surface_from_xdg_toplevel_resource(resource);
	struct wlr_seat_client *seat =
		wlr_seat_client_from_resource(seat_resource);

	if (!surface->configured) {
		wl_resource_post_error(surface->toplevel->resource,
			ZXDG_SURFACE_V6_ERROR_NOT_CONSTRUCTED,
			"surface has not been configured yet");
		return;
	}

	if (!wlr_seat_validate_grab_serial(seat->seat, serial)) {
		wlr_log(L_DEBUG, "invalid serial for grab");
		return;
	}

	struct wlr_xdg_toplevel_v6_move_event event = {
		.surface = surface,
		.seat = seat,
		.serial = serial,
	};

	wlr_signal_emit_safe(&surface->toplevel->events.request_move, &event);
}

static void xdg_toplevel_handle_resize(struct wl_client *client,
		struct wl_resource *resource, struct wl_resource *seat_resource,
		uint32_t serial, uint32_t edges) {
	struct wlr_xdg_surface_v6 *surface =
		xdg_surface_from_xdg_toplevel_resource(resource);
	struct wlr_seat_client *seat =
		wlr_seat_client_from_resource(seat_resource);

	if (!surface->configured) {
		wl_resource_post_error(surface->toplevel->resource,
			ZXDG_SURFACE_V6_ERROR_NOT_CONSTRUCTED,
			"surface has not been configured yet");
		return;
	}

	if (!wlr_seat_validate_grab_serial(seat->seat, serial)) {
		wlr_log(L_DEBUG, "invalid serial for grab");
		return;
	}

	struct wlr_xdg_toplevel_v6_resize_event event = {
		.surface = surface,
		.seat = seat,
		.serial = serial,
		.edges = edges,
	};

	wlr_signal_emit_safe(&surface->toplevel->events.request_resize, &event);
}

static void xdg_toplevel_handle_set_max_size(struct wl_client *client,
		struct wl_resource *resource, int32_t width, int32_t height) {
	struct wlr_xdg_surface_v6 *surface =
		xdg_surface_from_xdg_toplevel_resource(resource);
	surface->toplevel->client_pending.max_width = width;
	surface->toplevel->client_pending.max_height = height;
}

static void xdg_toplevel_handle_set_min_size(struct wl_client *client,
		struct wl_resource *resource, int32_t width, int32_t height) {
	struct wlr_xdg_surface_v6 *surface =
		xdg_surface_from_xdg_toplevel_resource(resource);
	surface->toplevel->client_pending.min_width = width;
	surface->toplevel->client_pending.min_height = height;
}

static void xdg_toplevel_handle_set_maximized(struct wl_client *client,
		struct wl_resource *resource) {
	struct wlr_xdg_surface_v6 *surface =
		xdg_surface_from_xdg_toplevel_resource(resource);
	surface->toplevel->client_pending.maximized = true;
	wlr_signal_emit_safe(&surface->toplevel->events.request_maximize, surface);
}

static void xdg_toplevel_handle_unset_maximized(struct wl_client *client,
		struct wl_resource *resource) {
	struct wlr_xdg_surface_v6 *surface =
		xdg_surface_from_xdg_toplevel_resource(resource);
	surface->toplevel->client_pending.maximized = false;
	wlr_signal_emit_safe(&surface->toplevel->events.request_maximize, surface);
}

static void xdg_toplevel_handle_set_fullscreen(struct wl_client *client,
		struct wl_resource *resource, struct wl_resource *output_resource) {
	struct wlr_xdg_surface_v6 *surface =
		xdg_surface_from_xdg_toplevel_resource(resource);

	struct wlr_output *output = NULL;
	if (output_resource != NULL) {
		output = wlr_output_from_resource(output_resource);
	}

	surface->toplevel->client_pending.fullscreen = true;

	struct wlr_xdg_toplevel_v6_set_fullscreen_event event = {
		.surface = surface,
		.fullscreen = true,
		.output = output,
	};

	wlr_signal_emit_safe(&surface->toplevel->events.request_fullscreen, &event);
}

static void xdg_toplevel_handle_unset_fullscreen(struct wl_client *client,
		struct wl_resource *resource) {
	struct wlr_xdg_surface_v6 *surface =
		xdg_surface_from_xdg_toplevel_resource(resource);

	surface->toplevel->client_pending.fullscreen = false;

	struct wlr_xdg_toplevel_v6_set_fullscreen_event event = {
		.surface = surface,
		.fullscreen = false,
		.output = NULL,
	};

	wlr_signal_emit_safe(&surface->toplevel->events.request_fullscreen, &event);
}

static void xdg_toplevel_handle_set_minimized(struct wl_client *client,
		struct wl_resource *resource) {
	struct wlr_xdg_surface_v6 *surface =
		xdg_surface_from_xdg_toplevel_resource(resource);
	wlr_signal_emit_safe(&surface->toplevel->events.request_minimize, surface);
}

static const struct zxdg_toplevel_v6_interface
		zxdg_toplevel_v6_implementation = {
	.destroy = xdg_toplevel_handle_destroy,
	.set_parent = xdg_toplevel_handle_set_parent,
	.set_title = xdg_toplevel_handle_set_title,
	.set_app_id = xdg_toplevel_handle_set_app_id,
	.show_window_menu = xdg_toplevel_handle_show_window_menu,
	.move = xdg_toplevel_handle_move,
	.resize = xdg_toplevel_handle_resize,
	.set_max_size = xdg_toplevel_handle_set_max_size,
	.set_min_size = xdg_toplevel_handle_set_min_size,
	.set_maximized = xdg_toplevel_handle_set_maximized,
	.unset_maximized = xdg_toplevel_handle_unset_maximized,
	.set_fullscreen = xdg_toplevel_handle_set_fullscreen,
	.unset_fullscreen = xdg_toplevel_handle_unset_fullscreen,
	.set_minimized = xdg_toplevel_handle_set_minimized,
};

void handle_xdg_toplevel_v6_ack_configure(struct wlr_xdg_surface_v6 *surface,
		struct wlr_xdg_surface_v6_configure *configure) {
	assert(surface->role == WLR_XDG_SURFACE_V6_ROLE_TOPLEVEL);
	assert(configure->toplevel_state != NULL);

	surface->toplevel->current.maximized =
		configure->toplevel_state->maximized;
	surface->toplevel->current.fullscreen =
		configure->toplevel_state->fullscreen;
	surface->toplevel->current.resizing =
		configure->toplevel_state->resizing;
	surface->toplevel->current.activated =
		configure->toplevel_state->activated;
}

bool compare_xdg_surface_v6_toplevel_state(struct wlr_xdg_toplevel_v6 *state) {
	struct {
		struct wlr_xdg_toplevel_v6_state state;
		uint32_t width, height;
	} configured;

	// is pending state different from current state?
	if (!state->base->configured) {
		return false;
	}

	if (wl_list_empty(&state->base->configure_list)) {
		// last configure is actually the current state, just use it
		configured.state = state->current;
		configured.width = state->base->surface->current->width;
		configured.height = state->base->surface->current->width;
	} else {
		struct wlr_xdg_surface_v6_configure *configure =
			wl_container_of(state->base->configure_list.prev, configure, link);
		configured.state = *configure->toplevel_state;
		configured.width = configure->toplevel_state->width;
		configured.height = configure->toplevel_state->height;
	}

	if (state->server_pending.activated != configured.state.activated) {
		return false;
	}
	if (state->server_pending.fullscreen != configured.state.fullscreen) {
		return false;
	}
	if (state->server_pending.maximized != configured.state.maximized) {
		return false;
	}
	if (state->server_pending.resizing != configured.state.resizing) {
		return false;
	}

	if (state->server_pending.width == configured.width &&
			state->server_pending.height == configured.height) {
		return true;
	}

	if (state->server_pending.width == 0 && state->server_pending.height == 0) {
		return true;
	}

	return false;
}

void send_xdg_toplevel_v6_configure(struct wlr_xdg_surface_v6 *surface,
		struct wlr_xdg_surface_v6_configure *configure) {
	assert(surface->role == WLR_XDG_SURFACE_V6_ROLE_TOPLEVEL);

	configure->toplevel_state = malloc(sizeof(*configure->toplevel_state));
	if (configure->toplevel_state == NULL) {
		wlr_log(L_ERROR, "Allocation failed");
		wl_resource_post_no_memory(surface->toplevel->resource);
		return;
	}
	*configure->toplevel_state = surface->toplevel->server_pending;

	uint32_t *s;
	struct wl_array states;
	wl_array_init(&states);
	if (surface->toplevel->server_pending.maximized) {
		s = wl_array_add(&states, sizeof(uint32_t));
		if (!s) {
			wlr_log(L_ERROR,
				"Could not allocate state for maximized xdg_toplevel");
			goto error_out;
		}
		*s = ZXDG_TOPLEVEL_V6_STATE_MAXIMIZED;
	}
	if (surface->toplevel->server_pending.fullscreen) {
		s = wl_array_add(&states, sizeof(uint32_t));
		if (!s) {
			wlr_log(L_ERROR,
				"Could not allocate state for fullscreen xdg_toplevel");
			goto error_out;
		}
		*s = ZXDG_TOPLEVEL_V6_STATE_FULLSCREEN;
	}
	if (surface->toplevel->server_pending.resizing) {
		s = wl_array_add(&states, sizeof(uint32_t));
		if (!s) {
			wlr_log(L_ERROR,
				"Could not allocate state for resizing xdg_toplevel");
			goto error_out;
		}
		*s = ZXDG_TOPLEVEL_V6_STATE_RESIZING;
	}
	if (surface->toplevel->server_pending.activated) {
		s = wl_array_add(&states, sizeof(uint32_t));
		if (!s) {
			wlr_log(L_ERROR,
				"Could not allocate state for activated xdg_toplevel");
			goto error_out;
		}
		*s = ZXDG_TOPLEVEL_V6_STATE_ACTIVATED;
	}

	uint32_t width = surface->toplevel->server_pending.width;
	uint32_t height = surface->toplevel->server_pending.height;
	zxdg_toplevel_v6_send_configure(surface->toplevel->resource, width,
		height, &states);

	wl_array_release(&states);
	return;

error_out:
	wl_array_release(&states);
	wl_resource_post_no_memory(surface->toplevel->resource);
}

void handle_xdg_surface_v6_toplevel_committed(struct wlr_xdg_surface_v6 *surface) {
	assert(surface->role == WLR_XDG_SURFACE_V6_ROLE_TOPLEVEL);

	if (!surface->toplevel->added) {
		// on the first commit, send a configure request to tell the client it
		// is added
		schedule_xdg_surface_v6_configure(surface);
		surface->toplevel->added = true;
		return;
	}

	// update state that doesn't need compositor approval
	surface->toplevel->current.max_width =
		surface->toplevel->client_pending.max_width;
	surface->toplevel->current.min_width =
		surface->toplevel->client_pending.min_width;
	surface->toplevel->current.max_height =
		surface->toplevel->client_pending.max_height;
	surface->toplevel->current.min_height =
		surface->toplevel->client_pending.min_height;
}

static void xdg_toplevel_handle_resource_destroy(struct wl_resource *resource) {
	struct wlr_xdg_surface_v6 *surface =
		xdg_surface_from_xdg_toplevel_resource(resource);
	if (surface != NULL) {
		destroy_xdg_toplevel_v6(surface);
	}
}

void create_xdg_toplevel_v6(struct wlr_xdg_surface_v6 *xdg_surface,
		uint32_t id) {
	if (wlr_surface_set_role(xdg_surface->surface, XDG_TOPLEVEL_V6_ROLE,
			xdg_surface->resource, ZXDG_SHELL_V6_ERROR_ROLE)) {
		return;
	}

	xdg_surface->toplevel = calloc(1, sizeof(struct wlr_xdg_toplevel_v6));
	if (xdg_surface->toplevel == NULL) {
		wl_resource_post_no_memory(xdg_surface->resource);
		return;
	}
	wl_signal_init(&xdg_surface->toplevel->events.request_maximize);
	wl_signal_init(&xdg_surface->toplevel->events.request_fullscreen);
	wl_signal_init(&xdg_surface->toplevel->events.request_minimize);
	wl_signal_init(&xdg_surface->toplevel->events.request_move);
	wl_signal_init(&xdg_surface->toplevel->events.request_resize);
	wl_signal_init(&xdg_surface->toplevel->events.request_show_window_menu);
	wl_signal_init(&xdg_surface->toplevel->events.set_parent);

	xdg_surface->role = WLR_XDG_SURFACE_V6_ROLE_TOPLEVEL;
	xdg_surface->toplevel->base = xdg_surface;

	struct wl_resource *toplevel_resource = wl_resource_create(
		xdg_surface->client->client, &zxdg_toplevel_v6_interface,
		wl_resource_get_version(xdg_surface->resource), id);
	if (toplevel_resource == NULL) {
		free(xdg_surface->toplevel);
		wl_resource_post_no_memory(xdg_surface->resource);
		return;
	}
	xdg_surface->toplevel->resource = toplevel_resource;
	wl_resource_set_implementation(toplevel_resource,
		&zxdg_toplevel_v6_implementation, xdg_surface,
		xdg_toplevel_handle_resource_destroy);
}

uint32_t wlr_xdg_toplevel_v6_set_size(struct wlr_xdg_surface_v6 *surface,
		uint32_t width, uint32_t height) {
	assert(surface->role == WLR_XDG_SURFACE_V6_ROLE_TOPLEVEL);
	surface->toplevel->server_pending.width = width;
	surface->toplevel->server_pending.height = height;

	return schedule_xdg_surface_v6_configure(surface);
}

uint32_t wlr_xdg_toplevel_v6_set_activated(struct wlr_xdg_surface_v6 *surface,
		bool activated) {
	assert(surface->role == WLR_XDG_SURFACE_V6_ROLE_TOPLEVEL);
	surface->toplevel->server_pending.activated = activated;

	return schedule_xdg_surface_v6_configure(surface);
}

uint32_t wlr_xdg_toplevel_v6_set_maximized(struct wlr_xdg_surface_v6 *surface,
		bool maximized) {
	assert(surface->role == WLR_XDG_SURFACE_V6_ROLE_TOPLEVEL);
	surface->toplevel->server_pending.maximized = maximized;

	return schedule_xdg_surface_v6_configure(surface);
}

uint32_t wlr_xdg_toplevel_v6_set_fullscreen(struct wlr_xdg_surface_v6 *surface,
		bool fullscreen) {
	assert(surface->role == WLR_XDG_SURFACE_V6_ROLE_TOPLEVEL);
	surface->toplevel->server_pending.fullscreen = fullscreen;

	return schedule_xdg_surface_v6_configure(surface);
}

uint32_t wlr_xdg_toplevel_v6_set_resizing(struct wlr_xdg_surface_v6 *surface,
		bool resizing) {
	assert(surface->role == WLR_XDG_SURFACE_V6_ROLE_TOPLEVEL);
	surface->toplevel->server_pending.resizing = resizing;

	return schedule_xdg_surface_v6_configure(surface);
}
