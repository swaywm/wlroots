#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-server.h>
#include <wlr/types/wlr_xdg_shell_v6.h>
#include <wlr/types/wlr_surface.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/util/log.h>
#include "xdg-shell-unstable-v6-protocol.h"

static const char *wlr_desktop_xdg_toplevel_role = "xdg_toplevel";

static void resource_destroy(struct wl_client *client,
		struct wl_resource *resource) {
	// TODO: we probably need to do more than this
	wl_resource_destroy(resource);
}

static void xdg_toplevel_protocol_set_parent(struct wl_client *client,
		struct wl_resource *resource, struct wl_resource *parent_resource) {
	wlr_log(L_DEBUG, "TODO: toplevel set parent");
}

static void xdg_toplevel_protocol_set_title(struct wl_client *client,
		struct wl_resource *resource, const char *title) {
	struct wlr_xdg_surface_v6 *surface = wl_resource_get_user_data(resource);
	char *tmp;

	tmp = strdup(title);
	if (tmp == NULL) {
		return;
	}

	free(surface->title);
	surface->title = tmp;
}

static void xdg_toplevel_protocol_set_app_id(struct wl_client *client,
		struct wl_resource *resource, const char *app_id) {
	struct wlr_xdg_surface_v6 *surface = wl_resource_get_user_data(resource);
	char *tmp;

	tmp = strdup(app_id);
	if (tmp == NULL) {
		return;
	}

	free(surface->app_id);
	surface->app_id = tmp;
}

static void xdg_toplevel_protocol_show_window_menu(struct wl_client *client,
		struct wl_resource *resource, struct wl_resource *seat_resource,
		uint32_t serial, int32_t x, int32_t y) {
	struct wlr_xdg_surface_v6 *surface = wl_resource_get_user_data(resource);
	struct wlr_seat_handle *seat_handle =
		wl_resource_get_user_data(seat_resource);

	struct wlr_xdg_toplevel_v6_show_window_menu_event *event =
		calloc(1, sizeof(struct wlr_xdg_toplevel_v6_show_window_menu_event));
	if (event == NULL) {
		wl_client_post_no_memory(client);
		return;
	}

	event->client = client;
	event->surface = surface;
	event->seat_handle = seat_handle;
	event->serial = serial;
	event->x = x;
	event->y = y;

	wl_signal_emit(&surface->events.request_show_window_menu, event);

	free(event);
}

static void xdg_toplevel_protocol_move(struct wl_client *client,
		struct wl_resource *resource, struct wl_resource *seat_resource,
		uint32_t serial) {
	struct wlr_xdg_surface_v6 *surface = wl_resource_get_user_data(resource);
	struct wlr_seat_handle *seat_handle =
		wl_resource_get_user_data(seat_resource);

	struct wlr_xdg_toplevel_v6_move_event *event =
		calloc(1, sizeof(struct wlr_xdg_toplevel_v6_move_event));
	if (event == NULL) {
		wl_client_post_no_memory(client);
		return;
	}

	event->client = client;
	event->surface = surface;
	event->seat_handle = seat_handle;
	event->serial = serial;

	wl_signal_emit(&surface->events.request_move, event);

	free(event);
}

static void xdg_toplevel_protocol_resize(struct wl_client *client,
		struct wl_resource *resource, struct wl_resource *seat_resource,
		uint32_t serial, uint32_t edges) {
	struct wlr_xdg_surface_v6 *surface = wl_resource_get_user_data(resource);
	struct wlr_seat_handle *seat_handle =
		wl_resource_get_user_data(seat_resource);

	struct wlr_xdg_toplevel_v6_resize_event *event =
		calloc(1, sizeof(struct wlr_xdg_toplevel_v6_resize_event));
	if (event == NULL) {
		wl_client_post_no_memory(client);
		return;
	}

	event->client = client;
	event->surface = surface;
	event->seat_handle = seat_handle;
	event->serial = serial;
	event->edges = edges;

	wl_signal_emit(&surface->events.request_resize, event);

	free(event);
}

static void xdg_toplevel_protocol_set_max_size(struct wl_client *client,
		struct wl_resource *resource, int32_t width, int32_t height) {
	struct wlr_xdg_surface_v6 *surface = wl_resource_get_user_data(resource);
	surface->toplevel_state->next.max_width = width;
	surface->toplevel_state->next.max_height = height;
}

static void xdg_toplevel_protocol_set_min_size(struct wl_client *client,
		struct wl_resource *resource, int32_t width, int32_t height) {
	struct wlr_xdg_surface_v6 *surface = wl_resource_get_user_data(resource);
	surface->toplevel_state->next.min_width = width;
	surface->toplevel_state->next.min_height = height;
}

static void xdg_toplevel_protocol_set_maximized(struct wl_client *client,
		struct wl_resource *resource) {
	struct wlr_xdg_surface_v6 *surface = wl_resource_get_user_data(resource);
	surface->toplevel_state->next.maximized = true;
}

static void xdg_toplevel_protocol_unset_maximized(struct wl_client *client,
		struct wl_resource *resource) {
	struct wlr_xdg_surface_v6 *surface = wl_resource_get_user_data(resource);
	surface->toplevel_state->next.maximized = false;
}

static void xdg_toplevel_protocol_set_fullscreen(struct wl_client *client,
		struct wl_resource *resource, struct wl_resource *output_resource) {
	struct wlr_xdg_surface_v6 *surface = wl_resource_get_user_data(resource);
	surface->toplevel_state->next.fullscreen = true;
}

static void xdg_toplevel_protocol_unset_fullscreen(struct wl_client *client,
		struct wl_resource *resource) {
	struct wlr_xdg_surface_v6 *surface = wl_resource_get_user_data(resource);
	surface->toplevel_state->next.fullscreen = false;
}

static void xdg_toplevel_protocol_set_minimized(struct wl_client *client,
		struct wl_resource *resource) {
	struct wlr_xdg_surface_v6 *surface = wl_resource_get_user_data(resource);
	wl_signal_emit(&surface->events.request_minimize, surface);
}

static const struct zxdg_toplevel_v6_interface zxdg_toplevel_v6_implementation =
{
	.destroy = resource_destroy,
	.set_parent = xdg_toplevel_protocol_set_parent,
	.set_title = xdg_toplevel_protocol_set_title,
	.set_app_id = xdg_toplevel_protocol_set_app_id,
	.show_window_menu = xdg_toplevel_protocol_show_window_menu,
	.move = xdg_toplevel_protocol_move,
	.resize = xdg_toplevel_protocol_resize,
	.set_max_size = xdg_toplevel_protocol_set_max_size,
	.set_min_size = xdg_toplevel_protocol_set_min_size,
	.set_maximized = xdg_toplevel_protocol_set_maximized,
	.unset_maximized = xdg_toplevel_protocol_unset_maximized,
	.set_fullscreen = xdg_toplevel_protocol_set_fullscreen,
	.unset_fullscreen = xdg_toplevel_protocol_unset_fullscreen,
	.set_minimized = xdg_toplevel_protocol_set_minimized
};

static void xdg_surface_destroy(struct wlr_xdg_surface_v6 *surface) {
	wl_signal_emit(&surface->events.destroy, surface);
	wl_resource_set_user_data(surface->resource, NULL);
	wl_list_remove(&surface->link);
	wl_list_remove(&surface->surface_destroy_listener.link);
	wl_list_remove(&surface->surface_commit_listener.link);
	free(surface->geometry);
	free(surface->next_geometry);
	free(surface);
}

static void xdg_surface_resource_destroy(struct wl_resource *resource) {
	struct wlr_xdg_surface_v6 *surface = wl_resource_get_user_data(resource);
	if (surface != NULL) {
		xdg_surface_destroy(surface);
	}
}

static void xdg_surface_get_toplevel(struct wl_client *client,
		struct wl_resource *resource, uint32_t id) {
	struct wlr_xdg_surface_v6 *surface = wl_resource_get_user_data(resource);

	if (wlr_surface_set_role(surface->surface, wlr_desktop_xdg_toplevel_role,
			resource, ZXDG_SHELL_V6_ERROR_ROLE)) {
		return;
	}

	if (!(surface->toplevel_state =
			calloc(1, sizeof(struct wlr_xdg_toplevel_v6)))) {
		return;
	}

	surface->role = WLR_XDG_SURFACE_V6_ROLE_TOPLEVEL;
	surface->toplevel_state->base = surface;

	struct wl_resource *toplevel_resource = wl_resource_create(client,
		&zxdg_toplevel_v6_interface, wl_resource_get_version(resource), id);

	surface->toplevel_state->resource = toplevel_resource;

	wl_resource_set_implementation(toplevel_resource,
		&zxdg_toplevel_v6_implementation, surface, NULL);
}

static void xdg_surface_get_popup(struct wl_client *client,
		struct wl_resource *resource, uint32_t id, struct wl_resource *parent,
		struct wl_resource *wl_positioner) {
	wlr_log(L_DEBUG, "TODO xdg surface get popup");
}

static void copy_toplevel_state(struct wlr_xdg_toplevel_v6_state *src,
		struct wlr_xdg_toplevel_v6_state *dest) {
	dest->width = src->width;
	dest->height = src->height;
	dest->max_width = src->max_width;
	dest->max_height = src->max_height;
	dest->min_width = src->min_width;
	dest->min_height = src->min_height;

	dest->fullscreen = src->fullscreen;
	dest->resizing = src->resizing;
	dest->activated = src->activated;
	dest->maximized = src->maximized;
}

static void wlr_xdg_toplevel_v6_ack_configure(
		struct wlr_xdg_surface_v6 *surface,
		struct wlr_xdg_surface_v6_configure *configure) {
	assert(surface->role == WLR_XDG_SURFACE_V6_ROLE_TOPLEVEL);
	copy_toplevel_state(configure->state, &surface->toplevel_state->next);
}

static void xdg_surface_ack_configure(struct wl_client *client,
		struct wl_resource *resource, uint32_t serial) {
	struct wlr_xdg_surface_v6 *surface = wl_resource_get_user_data(resource);

	// TODO handle popups
	if (surface->role != WLR_XDG_SURFACE_V6_ROLE_TOPLEVEL) {
		return;
	}

	bool found = false;
	struct wlr_xdg_surface_v6_configure *configure, *tmp;
	wl_list_for_each_safe(configure, tmp, &surface->configure_list, link) {
		if (configure->serial < serial) {
			wl_list_remove(&configure->link);
			free(configure);
		} else if (configure->serial == serial) {
			wl_list_remove(&configure->link);
			found = true;
			break;
		} else {
			break;
		}
	}
	if (!found) {
		// TODO post error on the client resource
		return;
	}

	// TODO handle popups
	if (surface->role == WLR_XDG_SURFACE_V6_ROLE_TOPLEVEL) {
		wlr_xdg_toplevel_v6_ack_configure(surface, configure);
	}

	if (!surface->configured) {
		surface->configured = true;
		wl_signal_emit(&surface->client->shell->events.new_surface, surface);
	}

	wl_signal_emit(&surface->events.ack_configure, surface);

	free(configure);
}

static void xdg_surface_set_window_geometry(struct wl_client *client,
		struct wl_resource *resource, int32_t x, int32_t y, int32_t width,
		int32_t height) {
	struct wlr_xdg_surface_v6 *surface = wl_resource_get_user_data(resource);
	surface->has_next_geometry = true;
	surface->next_geometry->height = height;
	surface->next_geometry->width = width;
	surface->next_geometry->x = x;
	surface->next_geometry->y = y;
}

static const struct zxdg_surface_v6_interface zxdg_surface_v6_implementation = {
	.destroy = resource_destroy,
	.get_toplevel = xdg_surface_get_toplevel,
	.get_popup = xdg_surface_get_popup,
	.ack_configure = xdg_surface_ack_configure,
	.set_window_geometry = xdg_surface_set_window_geometry,
};

static void xdg_shell_create_positioner(struct wl_client *client,
		struct wl_resource *resource, uint32_t id) {
	wlr_log(L_DEBUG, "TODO: xdg shell create positioner");
}

static bool wlr_xdg_surface_v6_toplevel_state_compare(
		struct wlr_xdg_toplevel_v6 *state) {
	// is pending state different from current state?
	if (state->pending.activated != state->current.activated) {
		return false;
	}
	if (state->pending.fullscreen != state->current.fullscreen) {
		return false;
	}
	if (state->pending.maximized != state->current.maximized) {
		return false;
	}
	if (state->pending.resizing != state->current.resizing) {
		return false;
	}

	if ((uint32_t)state->base->geometry->width == state->pending.width &&
			(uint32_t)state->base->geometry->height == state->pending.height) {
		return true;
	}

	if (state->pending.width == 0 && state->pending.height == 0) {
		return true;
	}

	return false;
}

static void wlr_xdg_toplevel_v6_send_configure(
		struct wlr_xdg_surface_v6 *surface,
		struct wlr_xdg_surface_v6_configure *configure) {
	assert(surface->role == WLR_XDG_SURFACE_V6_ROLE_TOPLEVEL);
	uint32_t *s;
	struct wl_array states;

	configure->state = &surface->toplevel_state->pending;

	wl_array_init(&states);
	if (surface->toplevel_state->pending.maximized) {
		s = wl_array_add(&states, sizeof(uint32_t));
		*s = ZXDG_TOPLEVEL_V6_STATE_MAXIMIZED;
	}
	if (surface->toplevel_state->pending.fullscreen) {
		s = wl_array_add(&states, sizeof(uint32_t));
		*s = ZXDG_TOPLEVEL_V6_STATE_FULLSCREEN;
	}
	if (surface->toplevel_state->pending.resizing) {
		s = wl_array_add(&states, sizeof(uint32_t));
		*s = ZXDG_TOPLEVEL_V6_STATE_RESIZING;
	}
	if (surface->toplevel_state->pending.activated) {
		s = wl_array_add(&states, sizeof(uint32_t));
		*s = ZXDG_TOPLEVEL_V6_STATE_ACTIVATED;
	}

	zxdg_toplevel_v6_send_configure(surface->toplevel_state->resource,
		surface->toplevel_state->pending.width,
		surface->toplevel_state->pending.height,
		&states);

	wl_array_release(&states);
}

static void wlr_xdg_surface_send_configure(void *user_data) {
	struct wlr_xdg_surface_v6 *surface = user_data;
	struct wl_display *display = wl_client_get_display(surface->client->client);

	// TODO handle popups
	assert(surface->role == WLR_XDG_SURFACE_V6_ROLE_TOPLEVEL);

	surface->configure_idle = NULL;

	// TODO handle no memory
	struct wlr_xdg_surface_v6_configure *configure =
		calloc(1, sizeof(struct wlr_xdg_surface_v6_configure));
	wl_list_insert(surface->configure_list.prev, &configure->link);
	configure->serial = wl_display_next_serial(display);

	wlr_xdg_toplevel_v6_send_configure(surface, configure);

	zxdg_surface_v6_send_configure(surface->resource, configure->serial);
}

static void wlr_xdg_surface_v6_schedule_configure(
		struct wlr_xdg_surface_v6 *surface, bool force) {
	// TODO handle popups
	assert(surface->role == WLR_XDG_SURFACE_V6_ROLE_TOPLEVEL);

	struct wl_display *display = wl_client_get_display(surface->client->client);
	struct wl_event_loop *loop = wl_display_get_event_loop(display);

	bool pending_same = !force &&
		wlr_xdg_surface_v6_toplevel_state_compare(surface->toplevel_state);

	if (surface->configure_idle != NULL) {
		if (!pending_same) {
			// configure request already scheduled
			return;
		}

		// configure request not necessary anymore
		wl_event_source_remove(surface->configure_idle);
		surface->configure_idle = NULL;
	} else {
		if (pending_same) {
			// configure request not necessary
			return;
		}

		surface->configure_idle =
			wl_event_loop_add_idle(
				loop,
				wlr_xdg_surface_send_configure,
				surface);
	}
}

static void handle_wlr_surface_destroyed(struct wl_listener *listener,
		void *data) {
	struct wlr_xdg_surface_v6 *xdg_surface =
		wl_container_of(listener, xdg_surface, surface_destroy_listener);
	xdg_surface_destroy(xdg_surface);
}

static void wlr_xdg_surface_v6_toplevel_committed(
		struct wlr_xdg_surface_v6 *surface) {
	if (!surface->toplevel_state->added) {
		// on the first commit, send a configure request to tell the client it
		// is added
		wlr_xdg_surface_v6_schedule_configure(surface, true);
		surface->toplevel_state->added = true;
		return;
	}

	copy_toplevel_state(&surface->toplevel_state->next,
		&surface->toplevel_state->current);
}

static void handle_wlr_surface_committed(struct wl_listener *listener,
		void *data) {
	struct wlr_xdg_surface_v6 *surface =
		wl_container_of(listener, surface, surface_commit_listener);

	if (surface->has_next_geometry) {
		surface->has_next_geometry = false;
		surface->geometry->x = surface->next_geometry->x;
		surface->geometry->y = surface->next_geometry->y;
		surface->geometry->width = surface->next_geometry->width;
		surface->geometry->height = surface->next_geometry->height;
	}

	if (surface->role == WLR_XDG_SURFACE_V6_ROLE_TOPLEVEL) {
		wlr_xdg_surface_v6_toplevel_committed(surface);
	}

	wl_signal_emit(&surface->events.commit, surface);
}

static void xdg_shell_get_xdg_surface(struct wl_client *wl_client,
		struct wl_resource *client_resource, uint32_t id,
		struct wl_resource *surface_resource) {
	struct wlr_xdg_client_v6 *client =
		wl_resource_get_user_data(client_resource);
	struct wlr_xdg_surface_v6 *surface;
	if (!(surface = calloc(1, sizeof(struct wlr_xdg_surface_v6)))) {
		wl_client_post_no_memory(wl_client);
		return;
	}

	if (!(surface->geometry = calloc(1, sizeof(struct wlr_box)))) {
		free(surface);
		wl_client_post_no_memory(wl_client);
		return;
	}

	if (!(surface->next_geometry = calloc(1, sizeof(struct wlr_box)))) {
		free(surface->geometry);
		free(surface);
		wl_client_post_no_memory(wl_client);
		return;
	}

	surface->client = client;
	surface->role = WLR_XDG_SURFACE_V6_ROLE_NONE;
	surface->surface = wl_resource_get_user_data(surface_resource);
	surface->resource = wl_resource_create(wl_client,
		&zxdg_surface_v6_interface, wl_resource_get_version(client_resource),
		id);

	wl_list_init(&surface->configure_list);

	wl_signal_init(&surface->events.request_minimize);
	wl_signal_init(&surface->events.request_move);
	wl_signal_init(&surface->events.request_resize);
	wl_signal_init(&surface->events.request_show_window_menu);
	wl_signal_init(&surface->events.commit);
	wl_signal_init(&surface->events.destroy);
	wl_signal_init(&surface->events.ack_configure);
	wl_signal_init(&surface->events.ping_timeout);

	wl_signal_add(&surface->surface->signals.destroy,
		&surface->surface_destroy_listener);
	surface->surface_destroy_listener.notify = handle_wlr_surface_destroyed;

	wl_signal_add(&surface->surface->signals.commit,
			&surface->surface_commit_listener);
	surface->surface_commit_listener.notify = handle_wlr_surface_committed;

	wlr_log(L_DEBUG, "new xdg_surface %p (res %p)", surface, surface->resource);
	wl_resource_set_implementation(surface->resource,
		&zxdg_surface_v6_implementation, surface, xdg_surface_resource_destroy);
	wl_list_insert(&client->surfaces, &surface->link);
}

static void xdg_shell_pong(struct wl_client *wl_client,
		struct wl_resource *resource, uint32_t serial) {
	struct wlr_xdg_client_v6 *client = wl_resource_get_user_data(resource);

	if (client->ping_serial != serial) {
		return;
	}

	wl_event_source_timer_update(client->ping_timer, 0);
	client->ping_serial = 0;
}

static struct zxdg_shell_v6_interface xdg_shell_impl = {
	.create_positioner = xdg_shell_create_positioner,
	.get_xdg_surface = xdg_shell_get_xdg_surface,
	.pong = xdg_shell_pong,
};

static void wlr_xdg_client_v6_destroy(struct wl_resource *resource) {
	struct wlr_xdg_client_v6 *client = wl_resource_get_user_data(resource);
	struct wl_list *list = &client->surfaces;
	struct wl_list *link, *tmp;

	for (link = list->next, tmp = link->next;
			link != list;
			link = tmp, tmp = link->next) {
		wl_list_remove(link);
		wl_list_init(link);
	}

	if (client->ping_timer != NULL) {
		wl_event_source_remove(client->ping_timer);
	}

	wl_list_remove(&client->link);
	free(client);
}

static int wlr_xdg_client_v6_ping_timeout(void *user_data) {
	struct wlr_xdg_client_v6 *client = user_data;

	struct wlr_xdg_surface_v6 *surface;
	wl_list_for_each(surface, &client->surfaces, link) {
		wl_signal_emit(&surface->events.ping_timeout, surface);
	}

	client->ping_serial = 0;
	return 1;
}

static void xdg_shell_bind(struct wl_client *wl_client, void *_xdg_shell,
		uint32_t version, uint32_t id) {
	struct wlr_xdg_shell_v6 *xdg_shell = _xdg_shell;
	assert(wl_client && xdg_shell);
	if (version > 1) {
		wlr_log(L_ERROR,
			"Client requested unsupported xdg_shell_v6 version, disconnecting");
		wl_client_destroy(wl_client);
		return;
	}
	struct wlr_xdg_client_v6 *client =
		calloc(1, sizeof(struct wlr_xdg_client_v6));
	if (client == NULL) {
		wl_client_post_no_memory(wl_client);
		return;
	}

	wl_list_init(&client->surfaces);

	client->resource =
		wl_resource_create(wl_client, &zxdg_shell_v6_interface, version, id);
	client->client = wl_client;
	client->shell = xdg_shell;

	wl_resource_set_implementation(client->resource, &xdg_shell_impl, client,
		wlr_xdg_client_v6_destroy);
	wl_list_insert(&xdg_shell->clients, &client->link);

	struct wl_display *display = wl_client_get_display(client->client);
	struct wl_event_loop *loop = wl_display_get_event_loop(display);
	client->ping_timer = wl_event_loop_add_timer(loop,
		wlr_xdg_client_v6_ping_timeout, client);
	if (client->ping_timer == NULL) {
		wl_client_post_no_memory(client->client);
	}
}

struct wlr_xdg_shell_v6 *wlr_xdg_shell_v6_create(struct wl_display *display) {
	struct wlr_xdg_shell_v6 *xdg_shell =
		calloc(1, sizeof(struct wlr_xdg_shell_v6));
	if (!xdg_shell) {
		return NULL;
	}

	xdg_shell->ping_timeout = 10000;

	wl_list_init(&xdg_shell->clients);

	struct wl_global *wl_global = wl_global_create(display,
		&zxdg_shell_v6_interface, 1, xdg_shell, xdg_shell_bind);
	if (!wl_global) {
		free(xdg_shell);
		return NULL;
	}
	xdg_shell->wl_global = wl_global;

	wl_signal_init(&xdg_shell->events.new_surface);

	return xdg_shell;
}

void wlr_xdg_shell_v6_destroy(struct wlr_xdg_shell_v6 *xdg_shell) {
	if (!xdg_shell) {
		return;
	}
	// TODO: disconnect clients and destroy surfaces

	// TODO: this segfault (wl_display->registry_resource_list is not init)
	// wl_global_destroy(xdg_shell->wl_global);
	free(xdg_shell);
}

void wlr_xdg_surface_v6_ping(struct wlr_xdg_surface_v6 *surface) {
	if (surface->client->ping_serial != 0) {
		// already pinged
		return;
	}

	surface->client->ping_serial =
		wl_display_next_serial(wl_client_get_display(surface->client->client));
	wl_event_source_timer_update(surface->client->ping_timer,
		surface->client->shell->ping_timeout);
	zxdg_shell_v6_send_ping(surface->client->resource,
		surface->client->ping_serial);
}

void wlr_xdg_toplevel_v6_set_size(struct wlr_xdg_surface_v6 *surface,
		uint32_t width, uint32_t height) {
	assert(surface->role == WLR_XDG_SURFACE_V6_ROLE_TOPLEVEL);
	surface->toplevel_state->pending.width = width;
	surface->toplevel_state->pending.height = height;

	wlr_xdg_surface_v6_schedule_configure(surface, false);
}

void wlr_xdg_toplevel_v6_set_activated(struct wlr_xdg_surface_v6 *surface,
		bool activated) {
	assert(surface->role == WLR_XDG_SURFACE_V6_ROLE_TOPLEVEL);
	surface->toplevel_state->pending.activated = activated;

	wlr_xdg_surface_v6_schedule_configure(surface, false);
}

void wlr_xdg_toplevel_v6_set_maximized(struct wlr_xdg_surface_v6 *surface,
		bool maximized) {
	assert(surface->role == WLR_XDG_SURFACE_V6_ROLE_TOPLEVEL);
	surface->toplevel_state->pending.maximized = maximized;

	wlr_xdg_surface_v6_schedule_configure(surface, false);
}

void wlr_xdg_toplevel_v6_set_fullscreen(struct wlr_xdg_surface_v6 *surface,
		bool fullscreen) {
	assert(surface->role == WLR_XDG_SURFACE_V6_ROLE_TOPLEVEL);
	surface->toplevel_state->pending.fullscreen = fullscreen;

	wlr_xdg_surface_v6_schedule_configure(surface, false);
}

void wlr_xdg_toplevel_v6_set_resizing(struct wlr_xdg_surface_v6 *surface,
		bool resizing) {
	assert(surface->role == WLR_XDG_SURFACE_V6_ROLE_TOPLEVEL);
	surface->toplevel_state->pending.fullscreen = resizing;

	wlr_xdg_surface_v6_schedule_configure(surface, false);
}
