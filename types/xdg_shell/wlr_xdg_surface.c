#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <wlr/util/box.h>
#include <wlr/util/log.h>
#include "types/wlr_xdg_shell.h"
#include "util/signal.h"

bool wlr_surface_is_xdg_surface(struct wlr_surface *surface) {
	return surface->role == &xdg_toplevel_surface_role ||
		surface->role == &xdg_popup_surface_role;
}

struct wlr_xdg_surface *wlr_xdg_surface_from_wlr_surface(
		struct wlr_surface *surface) {
	assert(wlr_surface_is_xdg_surface(surface));
	return (struct wlr_xdg_surface *)surface->role_data;
}

static void xdg_surface_configure_destroy(
		struct wlr_xdg_surface_configure *configure) {
	if (configure == NULL) {
		return;
	}
	wl_list_remove(&configure->link);
	free(configure->toplevel_configure);
	free(configure);
}

void unmap_xdg_surface(struct wlr_xdg_surface *surface) {
	assert(surface->role != WLR_XDG_SURFACE_ROLE_NONE);

	struct wlr_xdg_popup *popup, *popup_tmp;
	wl_list_for_each_safe(popup, popup_tmp, &surface->popups, link) {
		wlr_xdg_popup_destroy(popup->base);
	}

	// TODO: probably need to ungrab before this event
	if (surface->mapped) {
		wlr_signal_emit_safe(&surface->events.unmap, surface);
	}

	switch (surface->role) {
	case WLR_XDG_SURFACE_ROLE_TOPLEVEL:
		if (surface->toplevel->parent) {
			wl_list_remove(&surface->toplevel->parent_unmap.link);
			surface->toplevel->parent = NULL;
		}
		free(surface->toplevel->title);
		surface->toplevel->title = NULL;
		free(surface->toplevel->app_id);
		surface->toplevel->app_id = NULL;
		break;
	case WLR_XDG_SURFACE_ROLE_POPUP:
		if (surface->popup->seat != NULL) {
			struct wlr_xdg_popup_grab *grab =
				get_xdg_shell_popup_grab_from_seat(surface->client->shell,
					surface->popup->seat);

			wl_list_remove(&surface->popup->grab_link);

			if (wl_list_empty(&grab->popups)) {
				if (grab->seat->pointer_state.grab == &grab->pointer_grab) {
					wlr_seat_pointer_end_grab(grab->seat);
				}
				if (grab->seat->keyboard_state.grab == &grab->keyboard_grab) {
					wlr_seat_keyboard_end_grab(grab->seat);
				}
			}

			surface->popup->seat = NULL;
		}
		break;
	case WLR_XDG_SURFACE_ROLE_NONE:
		assert(false && "not reached");
	}

	struct wlr_xdg_surface_configure *configure, *tmp;
	wl_list_for_each_safe(configure, tmp, &surface->configure_list, link) {
		xdg_surface_configure_destroy(configure);
	}

	surface->configured = surface->mapped = false;
	surface->configure_serial = 0;
	if (surface->configure_idle) {
		wl_event_source_remove(surface->configure_idle);
		surface->configure_idle = NULL;
	}
	surface->configure_next_serial = 0;

	surface->has_next_geometry = false;
	memset(&surface->geometry, 0, sizeof(struct wlr_box));
	memset(&surface->next_geometry, 0, sizeof(struct wlr_box));
}


static void xdg_surface_handle_ack_configure(struct wl_client *client,
		struct wl_resource *resource, uint32_t serial) {
	struct wlr_xdg_surface *surface = wlr_xdg_surface_from_resource(resource);
	if (surface == NULL) {
		return;
	}

	if (surface->role == WLR_XDG_SURFACE_ROLE_NONE) {
		wl_resource_post_error(surface->resource,
			XDG_SURFACE_ERROR_NOT_CONSTRUCTED,
			"xdg_surface must have a role");
		return;
	}

	// First find the ack'ed configure
	bool found = false;
	struct wlr_xdg_surface_configure *configure, *tmp;
	wl_list_for_each(configure, &surface->configure_list, link) {
		if (configure->serial == serial) {
			found = true;
			break;
		}
	}
	if (!found) {
		wl_resource_post_error(surface->client->resource,
			XDG_WM_BASE_ERROR_INVALID_SURFACE_STATE,
			"wrong configure serial: %u", serial);
		return;
	}
	// Then remove old configures from the list
	wl_list_for_each_safe(configure, tmp, &surface->configure_list, link) {
		if (configure->serial == serial) {
			break;
		}
		wlr_signal_emit_safe(&surface->events.ack_configure, configure);
		xdg_surface_configure_destroy(configure);
	}

	switch (surface->role) {
	case WLR_XDG_SURFACE_ROLE_NONE:
		assert(0 && "not reached");
		break;
	case WLR_XDG_SURFACE_ROLE_TOPLEVEL:
		handle_xdg_toplevel_ack_configure(surface, configure);
		break;
	case WLR_XDG_SURFACE_ROLE_POPUP:
		break;
	}

	surface->configured = true;
	surface->configure_serial = serial;

	wlr_signal_emit_safe(&surface->events.ack_configure, configure);
	xdg_surface_configure_destroy(configure);
}

static void surface_send_configure(void *user_data) {
	struct wlr_xdg_surface *surface = user_data;

	surface->configure_idle = NULL;

	struct wlr_xdg_surface_configure *configure =
		calloc(1, sizeof(struct wlr_xdg_surface_configure));
	if (configure == NULL) {
		wl_client_post_no_memory(surface->client->client);
		return;
	}

	wl_list_insert(surface->configure_list.prev, &configure->link);
	configure->serial = surface->configure_next_serial;
	configure->surface = surface;

	switch (surface->role) {
	case WLR_XDG_SURFACE_ROLE_NONE:
		assert(0 && "not reached");
		break;
	case WLR_XDG_SURFACE_ROLE_TOPLEVEL:
		send_xdg_toplevel_configure(surface, configure);
		break;
	case WLR_XDG_SURFACE_ROLE_POPUP:
		xdg_popup_send_configure(surface->popup->resource,
			surface->popup->geometry.x,
			surface->popup->geometry.y,
			surface->popup->geometry.width,
			surface->popup->geometry.height);
		break;
	}

	wlr_signal_emit_safe(&surface->events.configure, configure);

	xdg_surface_send_configure(surface->resource, configure->serial);
}

uint32_t wlr_xdg_surface_schedule_configure(struct wlr_xdg_surface *surface) {
	struct wl_display *display = wl_client_get_display(surface->client->client);
	struct wl_event_loop *loop = wl_display_get_event_loop(display);

	if (surface->configure_idle == NULL) {
		surface->configure_next_serial = wl_display_next_serial(display);
		surface->configure_idle = wl_event_loop_add_idle(loop,
			surface_send_configure, surface);
		if (surface->configure_idle == NULL) {
			wl_client_post_no_memory(surface->client->client);
		}
	}
	return surface->configure_next_serial;
}

static void xdg_surface_handle_get_popup(struct wl_client *client,
		struct wl_resource *resource, uint32_t id,
		struct wl_resource *parent_resource,
		struct wl_resource *positioner_resource) {
	struct wlr_xdg_surface *xdg_surface =
		wlr_xdg_surface_from_resource(resource);
	struct wlr_xdg_surface *parent = NULL;
	if (parent_resource != NULL) {
		parent = wlr_xdg_surface_from_resource(parent_resource);
	}
	if (xdg_surface == NULL) {
		return; // TODO: create an inert xdg_popup
	}
	struct wlr_xdg_positioner_resource *positioner =
		get_xdg_positioner_from_resource(positioner_resource);
	create_xdg_popup(xdg_surface, parent, positioner, id);
}

static void xdg_surface_handle_get_toplevel(struct wl_client *client,
		struct wl_resource *resource, uint32_t id) {
	struct wlr_xdg_surface *xdg_surface =
		wlr_xdg_surface_from_resource(resource);
	if (xdg_surface == NULL) {
		return; // TODO: create an inert xdg_toplevel
	}
	create_xdg_toplevel(xdg_surface, id);
}

static void xdg_surface_handle_set_window_geometry(struct wl_client *client,
		struct wl_resource *resource, int32_t x, int32_t y, int32_t width,
		int32_t height) {
	struct wlr_xdg_surface *surface = wlr_xdg_surface_from_resource(resource);
	if (surface == NULL) {
		return;
	}

	if (surface->role == WLR_XDG_SURFACE_ROLE_NONE) {
		wl_resource_post_error(surface->resource,
			XDG_SURFACE_ERROR_NOT_CONSTRUCTED,
			"xdg_surface must have a role");
		return;
	}

	if (width <= 0 || height <= 0) {
		wlr_log(WLR_ERROR, "Client tried to set invalid geometry");
		//XXX: Switch to the proper error value once available
		wl_resource_post_error(resource, -1, "Tried to set invalid xdg-surface geometry");
		return;
	}

	surface->has_next_geometry = true;
	surface->next_geometry.height = height;
	surface->next_geometry.width = width;
	surface->next_geometry.x = x;
	surface->next_geometry.y = y;
}

static void xdg_surface_handle_destroy(struct wl_client *client,
		struct wl_resource *resource) {
	struct wlr_xdg_surface *surface = wlr_xdg_surface_from_resource(resource);
	if (surface == NULL) {
		return;
	}

	if (surface->role != WLR_XDG_SURFACE_ROLE_NONE) {
		wlr_log(WLR_ERROR, "Tried to destroy an xdg_surface before its role "
			"object");
		return;
	}

	wl_resource_destroy(resource);
}

static const struct xdg_surface_interface xdg_surface_implementation = {
	.destroy = xdg_surface_handle_destroy,
	.get_toplevel = xdg_surface_handle_get_toplevel,
	.get_popup = xdg_surface_handle_get_popup,
	.ack_configure = xdg_surface_handle_ack_configure,
	.set_window_geometry = xdg_surface_handle_set_window_geometry,
};

static void xdg_surface_handle_resource_destroy(struct wl_resource *resource) {
	struct wlr_xdg_surface *surface =
		wlr_xdg_surface_from_resource(resource);
	if (surface != NULL) {
		destroy_xdg_surface(surface);
	}
}

static void xdg_surface_handle_surface_commit(struct wl_listener *listener,
		void *data) {
	struct wlr_xdg_surface *surface =
		wl_container_of(listener, surface, surface_commit);

	if (wlr_surface_has_buffer(surface->surface) && !surface->configured) {
		wl_resource_post_error(surface->resource,
			XDG_SURFACE_ERROR_UNCONFIGURED_BUFFER,
			"xdg_surface has never been configured");
		return;
	}

	// surface->role might be NONE for inert popups
	// So we check surface->surface->role
	if (surface->surface->role == NULL) {
		wl_resource_post_error(surface->resource,
			XDG_SURFACE_ERROR_NOT_CONSTRUCTED,
			"xdg_surface must have a role");
		return;
	}
}

void handle_xdg_surface_commit(struct wlr_surface *wlr_surface) {
	struct wlr_xdg_surface *surface =
		wlr_xdg_surface_from_wlr_surface(wlr_surface);
	if (surface == NULL) {
		return;
	}

	if (surface->has_next_geometry) {
		surface->has_next_geometry = false;
		surface->geometry.x = surface->next_geometry.x;
		surface->geometry.y = surface->next_geometry.y;
		surface->geometry.width = surface->next_geometry.width;
		surface->geometry.height = surface->next_geometry.height;
	}

	switch (surface->role) {
	case WLR_XDG_SURFACE_ROLE_NONE:
		// inert toplevel or popup
		return;
	case WLR_XDG_SURFACE_ROLE_TOPLEVEL:
		handle_xdg_surface_toplevel_committed(surface);
		break;
	case WLR_XDG_SURFACE_ROLE_POPUP:
		handle_xdg_surface_popup_committed(surface);
		break;
	}

	if (!surface->added) {
		surface->added = true;
		wlr_signal_emit_safe(&surface->client->shell->events.new_surface,
			surface);
	}
	if (surface->configured && wlr_surface_has_buffer(surface->surface) &&
			!surface->mapped) {
		surface->mapped = true;
		wlr_signal_emit_safe(&surface->events.map, surface);
	}
	if (surface->configured && !wlr_surface_has_buffer(surface->surface) &&
			surface->mapped) {
		unmap_xdg_surface(surface);
	}
}

void handle_xdg_surface_precommit(struct wlr_surface *wlr_surface) {
	struct wlr_xdg_surface *surface =
		wlr_xdg_surface_from_wlr_surface(wlr_surface);
	if (surface == NULL) {
		return;
	}

	if (wlr_surface->pending.committed & WLR_SURFACE_STATE_BUFFER &&
			wlr_surface->pending.buffer == NULL) {
		// This is a NULL commit
		if (surface->configured && surface->mapped) {
			unmap_xdg_surface(surface);
		}
	}
}

static void xdg_surface_handle_surface_destroy(struct wl_listener *listener,
		void *data) {
	struct wlr_xdg_surface *xdg_surface =
		wl_container_of(listener, xdg_surface, surface_destroy);
	destroy_xdg_surface(xdg_surface);
}

struct wlr_xdg_surface *create_xdg_surface(
		struct wlr_xdg_client *client, struct wlr_surface *surface,
		uint32_t id) {
	struct wlr_xdg_surface *xdg_surface =
		calloc(1, sizeof(struct wlr_xdg_surface));
	if (xdg_surface == NULL) {
		wl_client_post_no_memory(client->client);
		return NULL;
	}

	xdg_surface->client = client;
	xdg_surface->role = WLR_XDG_SURFACE_ROLE_NONE;
	xdg_surface->surface = surface;
	xdg_surface->resource = wl_resource_create(client->client,
		&xdg_surface_interface, wl_resource_get_version(client->resource),
		id);
	if (xdg_surface->resource == NULL) {
		free(xdg_surface);
		wl_client_post_no_memory(client->client);
		return NULL;
	}

	if (wlr_surface_has_buffer(xdg_surface->surface)) {
		wl_resource_destroy(xdg_surface->resource);
		free(xdg_surface);
		wl_resource_post_error(client->resource,
			XDG_SURFACE_ERROR_UNCONFIGURED_BUFFER,
			"xdg_surface must not have a buffer at creation");
		return NULL;
	}

	wl_list_init(&xdg_surface->configure_list);
	wl_list_init(&xdg_surface->popups);

	wl_signal_init(&xdg_surface->events.destroy);
	wl_signal_init(&xdg_surface->events.ping_timeout);
	wl_signal_init(&xdg_surface->events.new_popup);
	wl_signal_init(&xdg_surface->events.map);
	wl_signal_init(&xdg_surface->events.unmap);
	wl_signal_init(&xdg_surface->events.configure);
	wl_signal_init(&xdg_surface->events.ack_configure);

	wl_signal_add(&xdg_surface->surface->events.destroy,
		&xdg_surface->surface_destroy);
	xdg_surface->surface_destroy.notify = xdg_surface_handle_surface_destroy;

	wl_signal_add(&xdg_surface->surface->events.commit,
		&xdg_surface->surface_commit);
	xdg_surface->surface_commit.notify = xdg_surface_handle_surface_commit;

	wlr_log(WLR_DEBUG, "new xdg_surface %p (res %p)", xdg_surface,
		xdg_surface->resource);
	wl_resource_set_implementation(xdg_surface->resource,
		&xdg_surface_implementation, xdg_surface,
		xdg_surface_handle_resource_destroy);
	wl_list_insert(&client->surfaces, &xdg_surface->link);

	return xdg_surface;
}

void reset_xdg_surface(struct wlr_xdg_surface *xdg_surface) {
	if (xdg_surface->role != WLR_XDG_SURFACE_ROLE_NONE) {
		unmap_xdg_surface(xdg_surface);
	}

	if (xdg_surface->added) {
		wlr_signal_emit_safe(&xdg_surface->events.destroy, xdg_surface);
		xdg_surface->added = false;
	}

	switch (xdg_surface->role) {
	case WLR_XDG_SURFACE_ROLE_TOPLEVEL:
		wl_resource_set_user_data(xdg_surface->toplevel->resource, NULL);
		xdg_surface->toplevel->resource = NULL;
		struct wlr_xdg_toplevel_requested *req =
			&xdg_surface->toplevel->requested;
		if (req->fullscreen_output) {
			wl_list_remove(&req->fullscreen_output_destroy.link);
		}
		free(xdg_surface->toplevel);
		xdg_surface->toplevel = NULL;
		break;
	case WLR_XDG_SURFACE_ROLE_POPUP:
		wl_resource_set_user_data(xdg_surface->popup->resource, NULL);
		xdg_surface->popup->resource = NULL;

		wl_list_remove(&xdg_surface->popup->link);

		free(xdg_surface->popup);
		xdg_surface->popup = NULL;
		break;
	case WLR_XDG_SURFACE_ROLE_NONE:
		// This space is intentionally left blank
		break;
	}

	xdg_surface->role = WLR_XDG_SURFACE_ROLE_NONE;
}

void destroy_xdg_surface(struct wlr_xdg_surface *surface) {
	reset_xdg_surface(surface);

	wl_resource_set_user_data(surface->resource, NULL);
	surface->surface->role_data = NULL;

	wl_list_remove(&surface->link);
	wl_list_remove(&surface->surface_destroy.link);
	wl_list_remove(&surface->surface_commit.link);
	free(surface);
}

struct wlr_xdg_surface *wlr_xdg_surface_from_resource(
		struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource, &xdg_surface_interface,
		&xdg_surface_implementation));
	return wl_resource_get_user_data(resource);
}

void wlr_xdg_surface_ping(struct wlr_xdg_surface *surface) {
	if (surface->client->ping_serial != 0) {
		// already pinged
		return;
	}

	surface->client->ping_serial =
		wl_display_next_serial(wl_client_get_display(surface->client->client));
	wl_event_source_timer_update(surface->client->ping_timer,
		surface->client->shell->ping_timeout);
	xdg_wm_base_send_ping(surface->client->resource,
		surface->client->ping_serial);
}

void wlr_xdg_toplevel_send_close(struct wlr_xdg_surface *surface) {
	assert(surface->toplevel);
	assert(surface->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL);
	xdg_toplevel_send_close(surface->toplevel->resource);
}

void wlr_xdg_popup_destroy(struct wlr_xdg_surface *surface) {
	if (surface == NULL) {
		return;
	}
	assert(surface->popup);
	assert(surface->role == WLR_XDG_SURFACE_ROLE_POPUP);

	struct wlr_xdg_popup *popup, *popup_tmp;
	wl_list_for_each_safe(popup, popup_tmp, &surface->popups, link) {
		wlr_xdg_popup_destroy(popup->base);
	}

	xdg_popup_send_popup_done(surface->popup->resource);
	wl_resource_set_user_data(surface->popup->resource, NULL);
	reset_xdg_surface(surface);
}

void wlr_xdg_popup_get_position(struct wlr_xdg_popup *popup,
		double *popup_sx, double *popup_sy) {
	struct wlr_xdg_surface *parent =
		wlr_xdg_surface_from_wlr_surface(popup->parent);
	struct wlr_box parent_geo;
	wlr_xdg_surface_get_geometry(parent, &parent_geo);
	*popup_sx = parent_geo.x + popup->geometry.x -
		popup->base->geometry.x;
	*popup_sy = parent_geo.y + popup->geometry.y -
		popup->base->geometry.y;
}

struct wlr_surface *wlr_xdg_surface_surface_at(
		struct wlr_xdg_surface *surface, double sx, double sy,
		double *sub_x, double *sub_y) {
	struct wlr_surface *sub = wlr_xdg_surface_popup_surface_at(surface, sx, sy,
			sub_x, sub_y);
	if (sub != NULL) {
		return sub;
	}
	return wlr_surface_surface_at(surface->surface, sx, sy, sub_x, sub_y);
}

struct wlr_surface *wlr_xdg_surface_popup_surface_at(
		struct wlr_xdg_surface *surface, double sx, double sy,
		double *sub_x, double *sub_y) {
	struct wlr_xdg_popup *popup_state;
	wl_list_for_each(popup_state, &surface->popups, link) {
		struct wlr_xdg_surface *popup = popup_state->base;

		double popup_sx, popup_sy;
		wlr_xdg_popup_get_position(popup_state, &popup_sx, &popup_sy);

		struct wlr_surface *sub = wlr_xdg_surface_surface_at(popup,
			sx - popup_sx,
			sy - popup_sy,
			sub_x, sub_y);
		if (sub != NULL) {
			return sub;
		}
	}

	return NULL;
}

struct xdg_surface_iterator_data {
	wlr_surface_iterator_func_t user_iterator;
	void *user_data;
	int x, y;
};

static void xdg_surface_iterator(struct wlr_surface *surface,
		int sx, int sy, void *data) {
	struct xdg_surface_iterator_data *iter_data = data;
	iter_data->user_iterator(surface, iter_data->x + sx, iter_data->y + sy,
		iter_data->user_data);
}

static void xdg_surface_for_each_popup_surface(struct wlr_xdg_surface *surface,
		int x, int y, wlr_surface_iterator_func_t iterator, void *user_data) {
	struct wlr_xdg_popup *popup_state;
	wl_list_for_each(popup_state, &surface->popups, link) {
		struct wlr_xdg_surface *popup = popup_state->base;
		if (!popup->configured) {
			continue;
		}

		double popup_sx, popup_sy;
		wlr_xdg_popup_get_position(popup_state, &popup_sx, &popup_sy);

		struct xdg_surface_iterator_data data = {
			.user_iterator = iterator,
			.user_data = user_data,
			.x = x + popup_sx, .y = y + popup_sy,
		};
		wlr_surface_for_each_surface(popup->surface, xdg_surface_iterator,
			&data);

		xdg_surface_for_each_popup_surface(popup,
			x + popup_sx,
			y + popup_sy,
			iterator, user_data);
	}
}

void wlr_xdg_surface_for_each_surface(struct wlr_xdg_surface *surface,
		wlr_surface_iterator_func_t iterator, void *user_data) {
	wlr_surface_for_each_surface(surface->surface, iterator, user_data);
	xdg_surface_for_each_popup_surface(surface, 0, 0, iterator, user_data);
}

void wlr_xdg_surface_for_each_popup_surface(struct wlr_xdg_surface *surface,
		wlr_surface_iterator_func_t iterator, void *user_data) {
	xdg_surface_for_each_popup_surface(surface, 0, 0, iterator, user_data);
}

void wlr_xdg_surface_get_geometry(struct wlr_xdg_surface *surface,
		struct wlr_box *box) {
	wlr_surface_get_extents(surface->surface, box);
	/* The client never set the geometry */
	if (!surface->geometry.width) {
		return;
	}

	wlr_box_intersection(box, &surface->geometry, box);
}
