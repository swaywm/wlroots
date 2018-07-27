#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <wlr/util/log.h>
#include "types/wlr_xdg_shell_v6.h"
#include "util/signal.h"

bool wlr_surface_is_xdg_surface_v6(struct wlr_surface *surface) {
	return surface->role == &xdg_toplevel_v6_surface_role ||
		surface->role == &xdg_popup_v6_surface_role;
}

struct wlr_xdg_surface_v6 *wlr_xdg_surface_v6_from_wlr_surface(
		struct wlr_surface *surface) {
	assert(wlr_surface_is_xdg_surface_v6(surface));
	return (struct wlr_xdg_surface_v6 *)surface->role_data;
}

static const struct zxdg_surface_v6_interface zxdg_surface_v6_implementation;

static struct wlr_xdg_surface_v6 *xdg_surface_from_resource(
		struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource, &zxdg_surface_v6_interface,
		&zxdg_surface_v6_implementation));
	return wl_resource_get_user_data(resource);
}

static void xdg_surface_configure_destroy(
		struct wlr_xdg_surface_v6_configure *configure) {
	if (configure == NULL) {
		return;
	}
	wl_list_remove(&configure->link);
	free(configure->toplevel_state);
	free(configure);
}

void unmap_xdg_surface_v6(struct wlr_xdg_surface_v6 *surface) {
	assert(surface->role != WLR_XDG_SURFACE_V6_ROLE_NONE);

	// TODO: probably need to ungrab before this event
	if (surface->mapped) {
		wlr_signal_emit_safe(&surface->events.unmap, surface);
	}

	switch (surface->role) {
	case WLR_XDG_SURFACE_V6_ROLE_TOPLEVEL:
		free(surface->toplevel->title);
		surface->toplevel->title = NULL;
		free(surface->toplevel->app_id);
		surface->toplevel->app_id = NULL;
		break;
	case WLR_XDG_SURFACE_V6_ROLE_POPUP:
		if (surface->popup->seat != NULL) {
			struct wlr_xdg_popup_grab_v6 *grab =
				get_xdg_shell_v6_popup_grab_from_seat(surface->client->shell,
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
	case WLR_XDG_SURFACE_V6_ROLE_NONE:
		assert(false && "not reached");
	}

	struct wlr_xdg_surface_v6_configure *configure, *tmp;
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

void destroy_xdg_surface_v6(struct wlr_xdg_surface_v6 *surface) {
	if (surface->role != WLR_XDG_SURFACE_V6_ROLE_NONE) {
		unmap_xdg_surface_v6(surface);
	}

	wlr_signal_emit_safe(&surface->events.destroy, surface);

	struct wlr_xdg_popup_v6 *popup_state, *next;
	wl_list_for_each_safe(popup_state, next, &surface->popups, link) {
		zxdg_popup_v6_send_popup_done(popup_state->resource);
		destroy_xdg_popup_v6(popup_state->base);
	}

	switch (surface->role) {
	case WLR_XDG_SURFACE_V6_ROLE_TOPLEVEL:
		destroy_xdg_toplevel_v6(surface);
		break;
	case WLR_XDG_SURFACE_V6_ROLE_POPUP:
		destroy_xdg_popup_v6(surface);
		break;
	case WLR_XDG_SURFACE_V6_ROLE_NONE:
		// This space is intentionally left blank
		break;
	}

	wl_resource_set_user_data(surface->resource, NULL);
	surface->surface->role_data = NULL;
	wl_list_remove(&surface->link);
	wl_list_remove(&surface->surface_destroy.link);
	wl_list_remove(&surface->surface_commit.link);
	free(surface);
}

static void xdg_surface_handle_get_toplevel(struct wl_client *client,
		struct wl_resource *resource, uint32_t id) {
	struct wlr_xdg_surface_v6 *xdg_surface = xdg_surface_from_resource(resource);
	create_xdg_toplevel_v6(xdg_surface, id);
}

static void xdg_surface_handle_get_popup(struct wl_client *wl_client,
		struct wl_resource *resource, uint32_t id,
		struct wl_resource *parent_resource,
		struct wl_resource *positioner_resource) {
	struct wlr_xdg_surface_v6 *xdg_surface =
		xdg_surface_from_resource(resource);
	struct wlr_xdg_surface_v6 *parent =
		xdg_surface_from_resource(parent_resource);
	struct wlr_xdg_positioner_v6_resource *positioner =
		get_xdg_positioner_v6_from_resource(positioner_resource);

	create_xdg_popup_v6(xdg_surface, parent, positioner, id);
}

static void xdg_surface_handle_ack_configure(struct wl_client *client,
		struct wl_resource *resource, uint32_t serial) {
	struct wlr_xdg_surface_v6 *surface = xdg_surface_from_resource(resource);

	if (surface->role == WLR_XDG_SURFACE_V6_ROLE_NONE) {
		wl_resource_post_error(surface->resource,
			ZXDG_SURFACE_V6_ERROR_NOT_CONSTRUCTED,
			"xdg_surface must have a role");
		return;
	}

	bool found = false;
	struct wlr_xdg_surface_v6_configure *configure, *tmp;
	wl_list_for_each_safe(configure, tmp, &surface->configure_list, link) {
		if (configure->serial < serial) {
			xdg_surface_configure_destroy(configure);
		} else if (configure->serial == serial) {
			found = true;
			break;
		} else {
			break;
		}
	}
	if (!found) {
		wl_resource_post_error(surface->client->resource,
			ZXDG_SHELL_V6_ERROR_INVALID_SURFACE_STATE,
			"wrong configure serial: %u", serial);
		return;
	}

	switch (surface->role) {
	case WLR_XDG_SURFACE_V6_ROLE_NONE:
		assert(0 && "not reached");
		break;
	case WLR_XDG_SURFACE_V6_ROLE_TOPLEVEL:
		handle_xdg_toplevel_v6_ack_configure(surface, configure);
		break;
	case WLR_XDG_SURFACE_V6_ROLE_POPUP:
		break;
	}

	surface->configured = true;
	surface->configure_serial = serial;

	xdg_surface_configure_destroy(configure);
}

static void xdg_surface_handle_set_window_geometry(struct wl_client *client,
		struct wl_resource *resource, int32_t x, int32_t y, int32_t width,
		int32_t height) {
	struct wlr_xdg_surface_v6 *surface = xdg_surface_from_resource(resource);

	if (surface->role == WLR_XDG_SURFACE_V6_ROLE_NONE) {
		wl_resource_post_error(surface->resource,
			ZXDG_SURFACE_V6_ERROR_NOT_CONSTRUCTED,
			"xdg_surface must have a role");
		return;
	}

	if (width <= 0 || height <= 0) {
		wlr_log(WLR_ERROR, "Client tried to set invalid geometry");
		wl_resource_post_error(resource, -1, "Tried to set invalid xdg-surface geometry");
	}


	surface->has_next_geometry = true;
	surface->next_geometry.height = height;
	surface->next_geometry.width = width;
	surface->next_geometry.x = x;
	surface->next_geometry.y = y;
}

static void xdg_surface_handle_destroy(struct wl_client *client,
		struct wl_resource *resource) {
	struct wlr_xdg_surface_v6 *surface = xdg_surface_from_resource(resource);

	if (surface->role != WLR_XDG_SURFACE_V6_ROLE_NONE) {
		wlr_log(WLR_ERROR, "Tried to destroy an xdg_surface before its role "
			"object");
		return;
	}

	wl_resource_destroy(resource);
}

static const struct zxdg_surface_v6_interface zxdg_surface_v6_implementation = {
	.destroy = xdg_surface_handle_destroy,
	.get_toplevel = xdg_surface_handle_get_toplevel,
	.get_popup = xdg_surface_handle_get_popup,
	.ack_configure = xdg_surface_handle_ack_configure,
	.set_window_geometry = xdg_surface_handle_set_window_geometry,
};

static void xdg_surface_send_configure(void *user_data) {
	struct wlr_xdg_surface_v6 *surface = user_data;

	surface->configure_idle = NULL;

	struct wlr_xdg_surface_v6_configure *configure =
		calloc(1, sizeof(struct wlr_xdg_surface_v6_configure));
	if (configure == NULL) {
		wl_client_post_no_memory(surface->client->client);
		return;
	}

	wl_list_insert(surface->configure_list.prev, &configure->link);
	configure->serial = surface->configure_next_serial;

	switch (surface->role) {
	case WLR_XDG_SURFACE_V6_ROLE_NONE:
		assert(0 && "not reached");
		break;
	case WLR_XDG_SURFACE_V6_ROLE_TOPLEVEL:
		send_xdg_toplevel_v6_configure(surface, configure);
		break;
	case WLR_XDG_SURFACE_V6_ROLE_POPUP:
		zxdg_popup_v6_send_configure(surface->popup->resource,
			surface->popup->geometry.x,
			surface->popup->geometry.y,
			surface->popup->geometry.width,
			surface->popup->geometry.height);
		break;
	}

	zxdg_surface_v6_send_configure(surface->resource, configure->serial);
}

uint32_t schedule_xdg_surface_v6_configure(struct wlr_xdg_surface_v6 *surface) {
	struct wl_display *display = wl_client_get_display(surface->client->client);
	struct wl_event_loop *loop = wl_display_get_event_loop(display);
	bool pending_same = false;

	switch (surface->role) {
	case WLR_XDG_SURFACE_V6_ROLE_NONE:
		assert(0 && "not reached");
		break;
	case WLR_XDG_SURFACE_V6_ROLE_TOPLEVEL:
		pending_same =
			compare_xdg_surface_v6_toplevel_state(surface->toplevel);
		break;
	case WLR_XDG_SURFACE_V6_ROLE_POPUP:
		break;
	}

	if (surface->configure_idle != NULL) {
		if (!pending_same) {
			// configure request already scheduled
			return surface->configure_next_serial;
		}

		// configure request not necessary anymore
		wl_event_source_remove(surface->configure_idle);
		surface->configure_idle = NULL;
		return 0;
	} else {
		if (pending_same) {
			// configure request not necessary
			return 0;
		}

		surface->configure_next_serial = wl_display_next_serial(display);
		surface->configure_idle = wl_event_loop_add_idle(loop,
			xdg_surface_send_configure, surface);
		return surface->configure_next_serial;
	}
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

void wlr_xdg_surface_v6_send_close(struct wlr_xdg_surface_v6 *surface) {
	switch (surface->role) {
	case WLR_XDG_SURFACE_V6_ROLE_NONE:
		assert(0 && "not reached");
		break;
	case WLR_XDG_SURFACE_V6_ROLE_TOPLEVEL:
		if (surface->toplevel) {
			zxdg_toplevel_v6_send_close(surface->toplevel->resource);
		}
		break;
	case WLR_XDG_SURFACE_V6_ROLE_POPUP:
		if (surface->popup) {
			zxdg_popup_v6_send_popup_done(surface->popup->resource);
		}
		break;
	}
}

static void xdg_surface_handle_surface_destroy(struct wl_listener *listener,
		void *data) {
	struct wlr_xdg_surface_v6 *xdg_surface =
		wl_container_of(listener, xdg_surface, surface_destroy);
	destroy_xdg_surface_v6(xdg_surface);
}

static void xdg_surface_handle_surface_commit(struct wl_listener *listener,
		void *data) {
	struct wlr_xdg_surface_v6 *surface =
		wl_container_of(listener, surface, surface_commit);

	if (wlr_surface_has_buffer(surface->surface) && !surface->configured) {
		wl_resource_post_error(surface->resource,
			ZXDG_SURFACE_V6_ERROR_UNCONFIGURED_BUFFER,
			"xdg_surface has never been configured");
		return;
	}

	if (surface->role == WLR_XDG_SURFACE_V6_ROLE_NONE) {
		wl_resource_post_error(surface->resource,
			ZXDG_SURFACE_V6_ERROR_NOT_CONSTRUCTED,
			"xdg_surface must have a role");
		return;
	}
}

void handle_xdg_surface_v6_commit(struct wlr_surface *wlr_surface) {
	struct wlr_xdg_surface_v6 *surface =
		wlr_xdg_surface_v6_from_wlr_surface(wlr_surface);
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
	case WLR_XDG_SURFACE_V6_ROLE_NONE:
		assert(false);
	case WLR_XDG_SURFACE_V6_ROLE_TOPLEVEL:
		handle_xdg_surface_v6_toplevel_committed(surface);
		break;
	case WLR_XDG_SURFACE_V6_ROLE_POPUP:
		handle_xdg_surface_v6_popup_committed(surface);
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
}

void handle_xdg_surface_v6_precommit(struct wlr_surface *wlr_surface) {
	struct wlr_xdg_surface_v6 *surface =
		wlr_xdg_surface_v6_from_wlr_surface(wlr_surface);
	if (surface == NULL) {
		return;
	}

	if (wlr_surface->pending.committed & WLR_SURFACE_STATE_BUFFER &&
			wlr_surface->pending.buffer_resource == NULL) {
		// This is a NULL commit
		if (surface->configured && surface->mapped) {
			unmap_xdg_surface_v6(surface);
		}
	}
}

static void xdg_surface_handle_resource_destroy(struct wl_resource *resource) {
	struct wlr_xdg_surface_v6 *surface = xdg_surface_from_resource(resource);
	if (surface != NULL) {
		destroy_xdg_surface_v6(surface);
	}
}

struct wlr_xdg_surface_v6 *create_xdg_surface_v6(
		struct wlr_xdg_client_v6 *client, struct wlr_surface *surface,
		uint32_t id) {
	if (wlr_surface_has_buffer(surface)) {
		wl_resource_post_error(client->resource,
			ZXDG_SURFACE_V6_ERROR_UNCONFIGURED_BUFFER,
			"xdg_surface must not have a buffer at creation");
		return NULL;
	}

	struct wlr_xdg_surface_v6 *xdg_surface =
		calloc(1, sizeof(struct wlr_xdg_surface_v6));
	if (xdg_surface == NULL) {
		wl_client_post_no_memory(client->client);
		return NULL;
	}

	xdg_surface->client = client;
	xdg_surface->role = WLR_XDG_SURFACE_V6_ROLE_NONE;
	xdg_surface->surface = surface;
	xdg_surface->resource = wl_resource_create(client->client,
		&zxdg_surface_v6_interface, wl_resource_get_version(client->resource),
		id);
	if (xdg_surface->resource == NULL) {
		free(xdg_surface);
		wl_client_post_no_memory(client->client);
		return NULL;
	}
	wl_resource_set_implementation(xdg_surface->resource,
		&zxdg_surface_v6_implementation, xdg_surface,
		xdg_surface_handle_resource_destroy);

	wl_list_init(&xdg_surface->configure_list);
	wl_list_init(&xdg_surface->popups);

	wl_signal_init(&xdg_surface->events.destroy);
	wl_signal_init(&xdg_surface->events.ping_timeout);
	wl_signal_init(&xdg_surface->events.new_popup);
	wl_signal_init(&xdg_surface->events.map);
	wl_signal_init(&xdg_surface->events.unmap);

	wl_signal_add(&xdg_surface->surface->events.destroy,
		&xdg_surface->surface_destroy);
	xdg_surface->surface_destroy.notify = xdg_surface_handle_surface_destroy;

	wl_signal_add(&xdg_surface->surface->events.commit,
		&xdg_surface->surface_commit);
	xdg_surface->surface_commit.notify = xdg_surface_handle_surface_commit;

	wlr_log(WLR_DEBUG, "new xdg_surface %p (res %p)", xdg_surface,
		xdg_surface->resource);
	wl_list_insert(&client->surfaces, &xdg_surface->link);

	return xdg_surface;
}

static void xdg_popup_v6_get_position(struct wlr_xdg_popup_v6 *popup,
		double *popup_sx, double *popup_sy) {
	struct wlr_xdg_surface_v6 *parent = popup->parent;
	struct wlr_box parent_geo;
	wlr_xdg_surface_v6_get_geometry(parent, &parent_geo);
	*popup_sx = parent_geo.x + popup->geometry.x -
		popup->base->geometry.x;
	*popup_sy = parent_geo.y + popup->geometry.y -
		popup->base->geometry.y;
}

struct wlr_surface *wlr_xdg_surface_v6_surface_at(
		struct wlr_xdg_surface_v6 *surface, double sx, double sy,
		double *sub_x, double *sub_y) {
	struct wlr_xdg_popup_v6 *popup_state;
	wl_list_for_each(popup_state, &surface->popups, link) {
		struct wlr_xdg_surface_v6 *popup = popup_state->base;

		double popup_sx, popup_sy;
		xdg_popup_v6_get_position(popup_state, &popup_sx, &popup_sy);

		struct wlr_surface *sub = wlr_xdg_surface_v6_surface_at(popup,
			sx - popup_sx,
			sy - popup_sy,
			sub_x, sub_y);
		if (sub != NULL) {
			return sub;
		}
	}

	return wlr_surface_surface_at(surface->surface, sx, sy, sub_x, sub_y);
}

struct xdg_surface_v6_iterator_data {
	wlr_surface_iterator_func_t user_iterator;
	void *user_data;
	int x, y;
};

static void xdg_surface_v6_iterator(struct wlr_surface *surface,
		int sx, int sy, void *data) {
	struct xdg_surface_v6_iterator_data *iter_data = data;
	iter_data->user_iterator(surface, iter_data->x + sx, iter_data->y + sy,
		iter_data->user_data);
}

static void xdg_surface_v6_for_each_surface(struct wlr_xdg_surface_v6 *surface,
		int x, int y, wlr_surface_iterator_func_t iterator, void *user_data) {
	struct xdg_surface_v6_iterator_data data = {
		.user_iterator = iterator,
		.user_data = user_data,
		.x = x, .y = y,
	};
	wlr_surface_for_each_surface(surface->surface, xdg_surface_v6_iterator,
		&data);

	struct wlr_xdg_popup_v6 *popup_state;
	wl_list_for_each(popup_state, &surface->popups, link) {
		struct wlr_xdg_surface_v6 *popup = popup_state->base;
		if (!popup->configured) {
			continue;
		}

		double popup_sx, popup_sy;
		xdg_popup_v6_get_position(popup_state, &popup_sx, &popup_sy);

		xdg_surface_v6_for_each_surface(popup,
			x + popup_sx,
			y + popup_sy,
			iterator, user_data);
	}
}

static void xdg_surface_v6_for_each_popup(struct wlr_xdg_surface_v6 *surface,
		int x, int y, wlr_surface_iterator_func_t iterator, void *user_data) {
	struct wlr_xdg_popup_v6 *popup_state;
	wl_list_for_each(popup_state, &surface->popups, link) {
		struct wlr_xdg_surface_v6 *popup = popup_state->base;
		if (!popup->configured) {
			continue;
		}

		double popup_sx, popup_sy;
		xdg_popup_v6_get_position(popup_state, &popup_sx, &popup_sy);
		iterator(popup->surface, x + popup_sx, y + popup_sy, user_data);

		xdg_surface_v6_for_each_popup(popup,
			x + popup_sx,
			y + popup_sy,
			iterator, user_data);
	}
}

void wlr_xdg_surface_v6_for_each_surface(struct wlr_xdg_surface_v6 *surface,
		wlr_surface_iterator_func_t iterator, void *user_data) {
	xdg_surface_v6_for_each_surface(surface, 0, 0, iterator, user_data);
}

void wlr_xdg_surface_v6_for_each_popup(struct wlr_xdg_surface_v6 *surface,
		wlr_surface_iterator_func_t iterator, void *user_data) {
	xdg_surface_v6_for_each_popup(surface, 0, 0, iterator, user_data);
}

void wlr_xdg_surface_v6_get_geometry(struct wlr_xdg_surface_v6 *surface, struct wlr_box *box) {
	wlr_surface_get_extends(surface->surface, box);
	/* The client never set the geometry */
	if (!surface->geometry.width) {
		return;
	}

	wlr_box_intersection(&surface->geometry, box, box);
}
