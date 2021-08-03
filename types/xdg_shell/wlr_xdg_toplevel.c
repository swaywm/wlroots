#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <wlr/util/log.h>
#include <wlr/util/edges.h>
#include "types/wlr_xdg_shell.h"
#include "util/signal.h"

void handle_xdg_toplevel_ack_configure(
		struct wlr_xdg_surface *surface,
		struct wlr_xdg_surface_configure *configure) {
	assert(surface->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL);
	assert(configure->toplevel_state != NULL);

	surface->toplevel->last_acked = *configure->toplevel_state;
}

bool compare_xdg_surface_toplevel_state(struct wlr_xdg_toplevel *state) {
	// is pending state different from current state?
	if (!state->base->configured) {
		return false;
	}

	struct wlr_xdg_toplevel_state *configured = NULL;
	if (wl_list_empty(&state->base->configure_list)) {
		// There are currently no pending configures, so check against the last
		// state acked by the client.
		configured = &state->last_acked;
	} else {
		struct wlr_xdg_surface_configure *configure =
			wl_container_of(state->base->configure_list.prev, configure, link);
		configured = configure->toplevel_state;
	}

	if (state->server_pending.activated != configured->activated) {
		return false;
	}
	if (state->server_pending.fullscreen != configured->fullscreen) {
		return false;
	}
	if (state->server_pending.maximized != configured->maximized) {
		return false;
	}
	if (state->server_pending.resizing != configured->resizing) {
		return false;
	}
	if (state->server_pending.tiled != configured->tiled) {
		return false;
	}

	if (state->server_pending.width == configured->width &&
			state->server_pending.height == configured->height) {
		return true;
	}

	if (state->server_pending.width == 0 && state->server_pending.height == 0) {
		return true;
	}

	return false;
}

void send_xdg_toplevel_configure(struct wlr_xdg_surface *surface,
		struct wlr_xdg_surface_configure *configure) {
	assert(surface->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL);

	configure->toplevel_state = malloc(sizeof(*configure->toplevel_state));
	if (configure->toplevel_state == NULL) {
		wlr_log(WLR_ERROR, "Allocation failed");
		wl_resource_post_no_memory(surface->toplevel->resource);
		return;
	}
	*configure->toplevel_state = surface->toplevel->server_pending;

	struct wl_array states;
	wl_array_init(&states);
	if (surface->toplevel->server_pending.maximized) {
		uint32_t *s = wl_array_add(&states, sizeof(uint32_t));
		if (!s) {
			wlr_log(WLR_ERROR, "Could not allocate state for maximized xdg_toplevel");
			goto error_out;
		}
		*s = XDG_TOPLEVEL_STATE_MAXIMIZED;
	}
	if (surface->toplevel->server_pending.fullscreen) {
		uint32_t *s = wl_array_add(&states, sizeof(uint32_t));
		if (!s) {
			wlr_log(WLR_ERROR, "Could not allocate state for fullscreen xdg_toplevel");
			goto error_out;
		}
		*s = XDG_TOPLEVEL_STATE_FULLSCREEN;
	}
	if (surface->toplevel->server_pending.resizing) {
		uint32_t *s = wl_array_add(&states, sizeof(uint32_t));
		if (!s) {
			wlr_log(WLR_ERROR, "Could not allocate state for resizing xdg_toplevel");
			goto error_out;
		}
		*s = XDG_TOPLEVEL_STATE_RESIZING;
	}
	if (surface->toplevel->server_pending.activated) {
		uint32_t *s = wl_array_add(&states, sizeof(uint32_t));
		if (!s) {
			wlr_log(WLR_ERROR, "Could not allocate state for activated xdg_toplevel");
			goto error_out;
		}
		*s = XDG_TOPLEVEL_STATE_ACTIVATED;
	}
	if (surface->toplevel->server_pending.tiled) {
		if (wl_resource_get_version(surface->resource) >=
				XDG_TOPLEVEL_STATE_TILED_LEFT_SINCE_VERSION) {
			const struct {
				enum wlr_edges edge;
				enum xdg_toplevel_state state;
			} tiled[] = {
				{ WLR_EDGE_LEFT, XDG_TOPLEVEL_STATE_TILED_LEFT },
				{ WLR_EDGE_RIGHT, XDG_TOPLEVEL_STATE_TILED_RIGHT },
				{ WLR_EDGE_TOP, XDG_TOPLEVEL_STATE_TILED_TOP },
				{ WLR_EDGE_BOTTOM, XDG_TOPLEVEL_STATE_TILED_BOTTOM },
			};

			for (size_t i = 0; i < sizeof(tiled)/sizeof(tiled[0]); ++i) {
				if ((surface->toplevel->server_pending.tiled &
						tiled[i].edge) == 0) {
					continue;
				}

				uint32_t *s = wl_array_add(&states, sizeof(uint32_t));
				if (!s) {
					wlr_log(WLR_ERROR,
						"Could not allocate state for tiled xdg_toplevel");
					goto error_out;
				}
				*s = tiled[i].state;
			}
		} else if (!surface->toplevel->server_pending.maximized) {
			// This version doesn't support tiling, best we can do is make the
			// toplevel maximized
			uint32_t *s = wl_array_add(&states, sizeof(uint32_t));
			if (!s) {
				wlr_log(WLR_ERROR,
					"Could not allocate state for maximized xdg_toplevel");
				goto error_out;
			}
			*s = XDG_TOPLEVEL_STATE_MAXIMIZED;
		}
	}

	uint32_t width = surface->toplevel->server_pending.width;
	uint32_t height = surface->toplevel->server_pending.height;
	xdg_toplevel_send_configure(surface->toplevel->resource, width, height,
		&states);

	wl_array_release(&states);
	return;

error_out:
	wl_array_release(&states);
	wl_resource_post_no_memory(surface->toplevel->resource);
}

void handle_xdg_surface_toplevel_committed(struct wlr_xdg_surface *surface) {
	assert(surface->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL);

	if (!surface->toplevel->added) {
		// on the first commit, send a configure request to tell the client it
		// is added
		schedule_xdg_surface_configure(surface);
		surface->toplevel->added = true;
		return;
	}

	struct wlr_surface_state_addon *addon;
	wl_list_for_each(addon, &surface->surface->current.addons, link) {
		if (addon->owner == surface->toplevel) {
			struct wlr_xdg_toplevel_state *state = addon->state;
			surface->toplevel->current = *state;
			break;
		}
	}
}


static const struct xdg_toplevel_interface xdg_toplevel_implementation;

struct wlr_xdg_surface *wlr_xdg_surface_from_toplevel_resource(
		struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource, &xdg_toplevel_interface,
		&xdg_toplevel_implementation));
	return wl_resource_get_user_data(resource);
}

static void handle_parent_unmap(struct wl_listener *listener, void *data) {
	struct wlr_xdg_toplevel *toplevel =
		wl_container_of(listener, toplevel, parent_unmap);
	wlr_xdg_toplevel_set_parent(toplevel->base,
			toplevel->parent->toplevel->parent);
}

void wlr_xdg_toplevel_set_parent(struct wlr_xdg_surface *surface,
		struct wlr_xdg_surface *parent) {
	assert(surface->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL);
	assert(!parent || parent->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL);

	if (surface->toplevel->parent) {
		wl_list_remove(&surface->toplevel->parent_unmap.link);
	}

	surface->toplevel->parent = parent;
	if (surface->toplevel->parent) {
		surface->toplevel->parent_unmap.notify = handle_parent_unmap;
		wl_signal_add(&surface->toplevel->parent->events.unmap,
				&surface->toplevel->parent_unmap);
	}

	wlr_signal_emit_safe(&surface->toplevel->events.set_parent, surface);
}

static void xdg_toplevel_handle_set_parent(struct wl_client *client,
		struct wl_resource *resource, struct wl_resource *parent_resource) {
	struct wlr_xdg_surface *surface =
		wlr_xdg_surface_from_toplevel_resource(resource);
	struct wlr_xdg_surface *parent = NULL;

	if (parent_resource != NULL) {
		parent = wlr_xdg_surface_from_toplevel_resource(parent_resource);
	}

	wlr_xdg_toplevel_set_parent(surface, parent);
}

static void xdg_toplevel_handle_set_title(struct wl_client *client,
		struct wl_resource *resource, const char *title) {
	struct wlr_xdg_surface *surface =
		wlr_xdg_surface_from_toplevel_resource(resource);
	char *tmp;

	tmp = strdup(title);
	if (tmp == NULL) {
		return;
	}

	free(surface->toplevel->title);
	surface->toplevel->title = tmp;
	wlr_signal_emit_safe(&surface->toplevel->events.set_title, surface);
}

static void xdg_toplevel_handle_set_app_id(struct wl_client *client,
		struct wl_resource *resource, const char *app_id) {
	struct wlr_xdg_surface *surface =
		wlr_xdg_surface_from_toplevel_resource(resource);
	char *tmp;

	tmp = strdup(app_id);
	if (tmp == NULL) {
		return;
	}

	free(surface->toplevel->app_id);
	surface->toplevel->app_id = tmp;
	wlr_signal_emit_safe(&surface->toplevel->events.set_app_id, surface);
}

static void xdg_toplevel_handle_show_window_menu(struct wl_client *client,
		struct wl_resource *resource, struct wl_resource *seat_resource,
		uint32_t serial, int32_t x, int32_t y) {
	struct wlr_xdg_surface *surface =
		wlr_xdg_surface_from_toplevel_resource(resource);
	struct wlr_seat_client *seat =
		wlr_seat_client_from_resource(seat_resource);

	if (!surface->configured) {
		wl_resource_post_error(surface->toplevel->resource,
			XDG_SURFACE_ERROR_NOT_CONSTRUCTED,
			"surface has not been configured yet");
		return;
	}

	if (!wlr_seat_validate_grab_serial(seat->seat, serial)) {
		wlr_log(WLR_DEBUG, "invalid serial for grab");
		return;
	}

	struct wlr_xdg_toplevel_show_window_menu_event event = {
		.surface = surface,
		.seat = seat,
		.serial = serial,
		.x = x,
		.y = y,
	};

	wlr_signal_emit_safe(&surface->toplevel->events.request_show_window_menu, &event);
}

static void xdg_toplevel_handle_move(struct wl_client *client,
		struct wl_resource *resource, struct wl_resource *seat_resource,
		uint32_t serial) {
	struct wlr_xdg_surface *surface =
		wlr_xdg_surface_from_toplevel_resource(resource);
	struct wlr_seat_client *seat =
		wlr_seat_client_from_resource(seat_resource);

	if (!surface->configured) {
		wl_resource_post_error(surface->toplevel->resource,
			XDG_SURFACE_ERROR_NOT_CONSTRUCTED,
			"surface has not been configured yet");
		return;
	}

	if (!wlr_seat_validate_grab_serial(seat->seat, serial)) {
		wlr_log(WLR_DEBUG, "invalid serial for grab");
		return;
	}

	struct wlr_xdg_toplevel_move_event event = {
		.surface = surface,
		.seat = seat,
		.serial = serial,
	};

	wlr_signal_emit_safe(&surface->toplevel->events.request_move, &event);
}

static void xdg_toplevel_handle_resize(struct wl_client *client,
		struct wl_resource *resource, struct wl_resource *seat_resource,
		uint32_t serial, uint32_t edges) {
	struct wlr_xdg_surface *surface =
		wlr_xdg_surface_from_toplevel_resource(resource);
	struct wlr_seat_client *seat =
		wlr_seat_client_from_resource(seat_resource);

	if (!surface->configured) {
		wl_resource_post_error(surface->toplevel->resource,
			XDG_SURFACE_ERROR_NOT_CONSTRUCTED,
			"surface has not been configured yet");
		return;
	}

	if (!wlr_seat_validate_grab_serial(seat->seat, serial)) {
		wlr_log(WLR_DEBUG, "invalid serial for grab");
		return;
	}

	struct wlr_xdg_toplevel_resize_event event = {
		.surface = surface,
		.seat = seat,
		.serial = serial,
		.edges = edges,
	};

	wlr_signal_emit_safe(&surface->toplevel->events.request_resize, &event);
}

static void xdg_toplevel_handle_set_max_size(struct wl_client *client,
		struct wl_resource *resource, int32_t width, int32_t height) {
	struct wlr_xdg_surface *surface =
		wlr_xdg_surface_from_toplevel_resource(resource);
	surface->toplevel->client_pending.max_width = width;
	surface->toplevel->client_pending.max_height = height;
}

static void xdg_toplevel_handle_set_min_size(struct wl_client *client,
		struct wl_resource *resource, int32_t width, int32_t height) {
	struct wlr_xdg_surface *surface =
		wlr_xdg_surface_from_toplevel_resource(resource);
	surface->toplevel->client_pending.min_width = width;
	surface->toplevel->client_pending.min_height = height;
}

static void xdg_toplevel_handle_set_maximized(struct wl_client *client,
		struct wl_resource *resource) {
	struct wlr_xdg_surface *surface =
		wlr_xdg_surface_from_toplevel_resource(resource);
	surface->toplevel->client_pending.maximized = true;
	wlr_signal_emit_safe(&surface->toplevel->events.request_maximize, surface);
}

static void xdg_toplevel_handle_unset_maximized(struct wl_client *client,
		struct wl_resource *resource) {
	struct wlr_xdg_surface *surface =
		wlr_xdg_surface_from_toplevel_resource(resource);
	surface->toplevel->client_pending.maximized = false;
	wlr_signal_emit_safe(&surface->toplevel->events.request_maximize, surface);
}

static void handle_fullscreen_output_destroy(struct wl_listener *listener,
		void *data) {
	struct wlr_xdg_toplevel_state *state =
		wl_container_of(listener, state, fullscreen_output_destroy);
	state->fullscreen_output = NULL;
	wl_list_remove(&state->fullscreen_output_destroy.link);
}

static void store_fullscreen_pending(struct wlr_xdg_surface *surface,
		bool fullscreen, struct wlr_output *output) {
	struct wlr_xdg_toplevel_state *state = &surface->toplevel->client_pending;
	state->fullscreen = fullscreen;
	if (state->fullscreen_output) {
		wl_list_remove(&state->fullscreen_output_destroy.link);
	}
	state->fullscreen_output = output;
	if (state->fullscreen_output) {
		state->fullscreen_output_destroy.notify =
			handle_fullscreen_output_destroy;
		wl_signal_add(&state->fullscreen_output->events.destroy,
				&state->fullscreen_output_destroy);
	}
}

static void xdg_toplevel_handle_set_fullscreen(struct wl_client *client,
		struct wl_resource *resource, struct wl_resource *output_resource) {
	struct wlr_xdg_surface *surface =
		wlr_xdg_surface_from_toplevel_resource(resource);

	struct wlr_output *output = NULL;
	if (output_resource != NULL) {
		output = wlr_output_from_resource(output_resource);
	}

	store_fullscreen_pending(surface, true, output);

	struct wlr_xdg_toplevel_set_fullscreen_event event = {
		.surface = surface,
		.fullscreen = true,
		.output = output,
	};

	wlr_signal_emit_safe(&surface->toplevel->events.request_fullscreen, &event);
}

static void xdg_toplevel_handle_unset_fullscreen(struct wl_client *client,
		struct wl_resource *resource) {
	struct wlr_xdg_surface *surface =
		wlr_xdg_surface_from_toplevel_resource(resource);

	store_fullscreen_pending(surface, false, NULL);

	struct wlr_xdg_toplevel_set_fullscreen_event event = {
		.surface = surface,
		.fullscreen = false,
		.output = NULL,
	};

	wlr_signal_emit_safe(&surface->toplevel->events.request_fullscreen, &event);
}

static void xdg_toplevel_handle_set_minimized(struct wl_client *client,
		struct wl_resource *resource) {
	struct wlr_xdg_surface *surface =
		wlr_xdg_surface_from_toplevel_resource(resource);
	wlr_signal_emit_safe(&surface->toplevel->events.request_minimize, surface);
}

static void xdg_toplevel_handle_destroy(struct wl_client *client,
		struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static const struct xdg_toplevel_interface xdg_toplevel_implementation = {
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

static void xdg_toplevel_finish_state(void *data) {
	struct wlr_xdg_toplevel_state *state = data;
	if (state->fullscreen_output) {
		wl_list_remove(&state->fullscreen_output_destroy.link);
	}
}

static void xdg_toplevel_handle_resource_destroy(struct wl_resource *resource) {
	struct wlr_xdg_surface *surface =
		wlr_xdg_surface_from_toplevel_resource(resource);
	destroy_xdg_toplevel(surface);
}

static void xdg_toplevel_handle_surface_precommit(struct wl_listener *listener,
		void *data) {
	struct wlr_xdg_toplevel *toplevel =
		wl_container_of(listener, toplevel, surface_precommit);
	struct wlr_surface_state *pending = data;

	struct wlr_surface_state_addon *addon = calloc(1, sizeof(*addon));
	struct wlr_xdg_toplevel_state *state = calloc(1, sizeof(*state));
	if (!addon || !state) {
		free(addon);
		free(state);
		return;
	}
	*state = toplevel->last_acked;

	// State from the client that doesn't need compositor approval
	state->max_width = toplevel->client_pending.max_width;
	state->min_width = toplevel->client_pending.min_width;
	state->max_height = toplevel->client_pending.max_height;
	state->min_height = toplevel->client_pending.min_height;

	addon->owner = toplevel;
	addon->state = state;
	addon->finish_state = xdg_toplevel_finish_state;
	wl_list_insert(&pending->addons, &addon->link);
}

const struct wlr_surface_role xdg_toplevel_surface_role = {
	.name = "xdg_toplevel",
	.commit = handle_xdg_surface_commit,
	.precommit = handle_xdg_surface_precommit,
};

void create_xdg_toplevel(struct wlr_xdg_surface *xdg_surface,
		uint32_t id) {
	if (!wlr_surface_set_role(xdg_surface->surface, &xdg_toplevel_surface_role,
			xdg_surface, xdg_surface->resource, XDG_WM_BASE_ERROR_ROLE)) {
		return;
	}

	if (xdg_surface->role != WLR_XDG_SURFACE_ROLE_NONE) {
		wl_resource_post_error(xdg_surface->resource,
			XDG_SURFACE_ERROR_ALREADY_CONSTRUCTED,
			"xdg-surface has already been constructed");
		return;
	}

	assert(xdg_surface->toplevel == NULL);
	xdg_surface->toplevel = calloc(1, sizeof(struct wlr_xdg_toplevel));
	if (xdg_surface->toplevel == NULL) {
		wl_resource_post_no_memory(xdg_surface->resource);
		return;
	}
	xdg_surface->toplevel->base = xdg_surface;

	wl_signal_init(&xdg_surface->toplevel->events.request_maximize);
	wl_signal_init(&xdg_surface->toplevel->events.request_fullscreen);
	wl_signal_init(&xdg_surface->toplevel->events.request_minimize);
	wl_signal_init(&xdg_surface->toplevel->events.request_move);
	wl_signal_init(&xdg_surface->toplevel->events.request_resize);
	wl_signal_init(&xdg_surface->toplevel->events.request_show_window_menu);
	wl_signal_init(&xdg_surface->toplevel->events.set_parent);
	wl_signal_init(&xdg_surface->toplevel->events.set_title);
	wl_signal_init(&xdg_surface->toplevel->events.set_app_id);

	xdg_surface->toplevel->resource = wl_resource_create(
		xdg_surface->client->client, &xdg_toplevel_interface,
		wl_resource_get_version(xdg_surface->resource), id);
	if (xdg_surface->toplevel->resource == NULL) {
		free(xdg_surface->toplevel);
		wl_resource_post_no_memory(xdg_surface->resource);
		return;
	}
	wl_resource_set_implementation(xdg_surface->toplevel->resource,
		&xdg_toplevel_implementation, xdg_surface,
		xdg_toplevel_handle_resource_destroy);

	wl_signal_add(&xdg_surface->surface->events.precommit,
		&xdg_surface->toplevel->surface_precommit);
	xdg_surface->toplevel->surface_precommit.notify =
		xdg_toplevel_handle_surface_precommit;

	xdg_surface->role = WLR_XDG_SURFACE_ROLE_TOPLEVEL;
}

void destroy_xdg_toplevel(struct wlr_xdg_surface *xdg_surface) {
	if (xdg_surface == NULL) {
		return;
	}
	assert(xdg_surface->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL);
	reset_xdg_surface(xdg_surface);
}

uint32_t wlr_xdg_toplevel_set_size(struct wlr_xdg_surface *surface,
		uint32_t width, uint32_t height) {
	assert(surface->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL);
	surface->toplevel->server_pending.width = width;
	surface->toplevel->server_pending.height = height;

	return schedule_xdg_surface_configure(surface);
}

uint32_t wlr_xdg_toplevel_set_activated(struct wlr_xdg_surface *surface,
		bool activated) {
	assert(surface->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL);
	surface->toplevel->server_pending.activated = activated;

	return schedule_xdg_surface_configure(surface);
}

uint32_t wlr_xdg_toplevel_set_maximized(struct wlr_xdg_surface *surface,
		bool maximized) {
	assert(surface->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL);
	surface->toplevel->server_pending.maximized = maximized;

	return schedule_xdg_surface_configure(surface);
}

uint32_t wlr_xdg_toplevel_set_fullscreen(struct wlr_xdg_surface *surface,
		bool fullscreen) {
	assert(surface->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL);
	surface->toplevel->server_pending.fullscreen = fullscreen;

	return schedule_xdg_surface_configure(surface);
}

uint32_t wlr_xdg_toplevel_set_resizing(struct wlr_xdg_surface *surface,
		bool resizing) {
	assert(surface->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL);
	surface->toplevel->server_pending.resizing = resizing;

	return schedule_xdg_surface_configure(surface);
}

uint32_t wlr_xdg_toplevel_set_tiled(struct wlr_xdg_surface *surface,
		uint32_t tiled) {
	assert(surface->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL);
	surface->toplevel->server_pending.tiled = tiled;

	return schedule_xdg_surface_configure(surface);
}
