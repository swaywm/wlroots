#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-server.h>
#include <wlr/config.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_surface.h>
#include <wlr/types/wlr_xdg_shell_v6.h>
#include <wlr/util/log.h>
#include "util/signal.h"
#include "xdg-shell-unstable-v6-protocol.h"

static const char *wlr_desktop_xdg_toplevel_role = "xdg_toplevel";
static const char *wlr_desktop_xdg_popup_role = "xdg_popup";

struct wlr_xdg_positioner_v6 {
	struct wl_resource *resource;

	struct wlr_box anchor_rect;
	enum zxdg_positioner_v6_anchor anchor;
	enum zxdg_positioner_v6_gravity gravity;
	enum zxdg_positioner_v6_constraint_adjustment constraint_adjustment;

	struct {
		int32_t width, height;
	} size;

	struct {
		int32_t x, y;
	} offset;
};


static void resource_handle_destroy(struct wl_client *client,
		struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static struct wlr_xdg_surface_v6 *xdg_popup_grab_get_topmost(
		struct wlr_xdg_popup_grab_v6 *grab) {
	struct wlr_xdg_popup_v6 *popup;
	wl_list_for_each(popup, &grab->popups, grab_link) {
		return popup->base;
	}

	return NULL;
}

static void xdg_pointer_grab_end(struct wlr_seat_pointer_grab *grab) {
	struct wlr_xdg_popup_grab_v6 *popup_grab = grab->data;

	struct wlr_xdg_popup_v6 *popup, *tmp;
	wl_list_for_each_safe(popup, tmp, &popup_grab->popups, grab_link) {
		zxdg_popup_v6_send_popup_done(popup->resource);
	}

	wlr_seat_pointer_end_grab(grab->seat);
}

static void xdg_pointer_grab_enter(struct wlr_seat_pointer_grab *grab,
		struct wlr_surface *surface, double sx, double sy) {
	struct wlr_xdg_popup_grab_v6 *popup_grab = grab->data;
	if (wl_resource_get_client(surface->resource) == popup_grab->client) {
		wlr_seat_pointer_enter(grab->seat, surface, sx, sy);
	} else {
		wlr_seat_pointer_clear_focus(grab->seat);
	}
}

static void xdg_pointer_grab_motion(struct wlr_seat_pointer_grab *grab,
		uint32_t time, double sx, double sy) {
	wlr_seat_pointer_send_motion(grab->seat, time, sx, sy);
}

static uint32_t xdg_pointer_grab_button(struct wlr_seat_pointer_grab *grab,
		uint32_t time, uint32_t button, uint32_t state) {
	uint32_t serial =
		wlr_seat_pointer_send_button(grab->seat, time, button, state);
	if (serial) {
		return serial;
	} else {
		xdg_pointer_grab_end(grab);
		return 0;
	}
}

static void xdg_pointer_grab_axis(struct wlr_seat_pointer_grab *grab,
		uint32_t time, enum wlr_axis_orientation orientation, double value) {
	wlr_seat_pointer_send_axis(grab->seat, time, orientation, value);
}

static void xdg_pointer_grab_cancel(struct wlr_seat_pointer_grab *grab) {
	xdg_pointer_grab_end(grab);
}

static const struct wlr_pointer_grab_interface xdg_pointer_grab_impl = {
	.enter = xdg_pointer_grab_enter,
	.motion = xdg_pointer_grab_motion,
	.button = xdg_pointer_grab_button,
	.cancel = xdg_pointer_grab_cancel,
	.axis = xdg_pointer_grab_axis,
};

static void xdg_keyboard_grab_enter(struct wlr_seat_keyboard_grab *grab,
		struct wlr_surface *surface, uint32_t keycodes[], size_t num_keycodes,
		struct wlr_keyboard_modifiers *modifiers) {
	// keyboard focus should remain on the popup
}

static void xdg_keyboard_grab_key(struct wlr_seat_keyboard_grab *grab, uint32_t time,
		uint32_t key, uint32_t state) {
	wlr_seat_keyboard_send_key(grab->seat, time, key, state);
}

static void xdg_keyboard_grab_modifiers(struct wlr_seat_keyboard_grab *grab,
		struct wlr_keyboard_modifiers *modifiers) {
	wlr_seat_keyboard_send_modifiers(grab->seat, modifiers);
}

static void xdg_keyboard_grab_cancel(struct wlr_seat_keyboard_grab *grab) {
	wlr_seat_keyboard_end_grab(grab->seat);
}

static const struct wlr_keyboard_grab_interface xdg_keyboard_grab_impl = {
	.enter = xdg_keyboard_grab_enter,
	.key = xdg_keyboard_grab_key,
	.modifiers = xdg_keyboard_grab_modifiers,
	.cancel = xdg_keyboard_grab_cancel,
};

static struct wlr_xdg_popup_grab_v6 *xdg_shell_popup_grab_from_seat(
		struct wlr_xdg_shell_v6 *shell, struct wlr_seat *seat) {
	struct wlr_xdg_popup_grab_v6 *xdg_grab;
	wl_list_for_each(xdg_grab, &shell->popup_grabs, link) {
		if (xdg_grab->seat == seat) {
			return xdg_grab;
		}
	}

	xdg_grab = calloc(1, sizeof(struct wlr_xdg_popup_grab_v6));
	if (!xdg_grab) {
		return NULL;
	}

	xdg_grab->pointer_grab.data = xdg_grab;
	xdg_grab->pointer_grab.interface = &xdg_pointer_grab_impl;
	xdg_grab->keyboard_grab.data = xdg_grab;
	xdg_grab->keyboard_grab.interface = &xdg_keyboard_grab_impl;

	wl_list_init(&xdg_grab->popups);

	wl_list_insert(&shell->popup_grabs, &xdg_grab->link);
	xdg_grab->seat = seat;

	return xdg_grab;
}


static void xdg_surface_unmap(struct wlr_xdg_surface_v6 *surface) {
	assert(surface->role != WLR_XDG_SURFACE_V6_ROLE_NONE);

	// TODO: probably need to ungrab before this event
	wlr_signal_emit_safe(&surface->events.unmap, surface);

	if (surface->role == WLR_XDG_SURFACE_V6_ROLE_TOPLEVEL) {
		wl_resource_set_user_data(surface->toplevel_state->resource, NULL);
		free(surface->toplevel_state);
		surface->toplevel_state = NULL;
	}

	if (surface->role == WLR_XDG_SURFACE_V6_ROLE_POPUP) {
		wl_resource_set_user_data(surface->popup_state->resource, NULL);

		if (surface->popup_state->seat) {
			struct wlr_xdg_popup_grab_v6 *grab =
				xdg_shell_popup_grab_from_seat(surface->client->shell,
					surface->popup_state->seat);

			wl_list_remove(&surface->popup_state->grab_link);

			if (wl_list_empty(&grab->popups)) {
				if (grab->seat->pointer_state.grab == &grab->pointer_grab) {
					wlr_seat_pointer_end_grab(grab->seat);
				}
				if (grab->seat->keyboard_state.grab == &grab->keyboard_grab) {
					wlr_seat_keyboard_end_grab(grab->seat);
				}
			}
		}

		wl_list_remove(&surface->popup_state->link);
		free(surface->popup_state);
		surface->popup_state = NULL;
	}

	surface->role = WLR_XDG_SURFACE_V6_ROLE_NONE;
	free(surface->title);
	surface->title = NULL;
	free(surface->app_id);
	surface->app_id = NULL;

	surface->added = surface->configured = surface->mapped = false;
	surface->configure_serial = 0;
	if (surface->configure_idle) {
		wl_event_source_remove(surface->configure_idle);
		surface->configure_idle = NULL;
	}
	surface->configure_next_serial = 0;

	struct wlr_xdg_surface_v6_configure *configure, *tmp;
	wl_list_for_each_safe(configure, tmp, &surface->configure_list, link) {
		wl_list_remove(&configure->link);
		free(configure);
	}

	surface->has_next_geometry = false;
	memset(surface->geometry, 0, sizeof(struct wlr_box));
	memset(surface->next_geometry, 0, sizeof(struct wlr_box));
}

static void xdg_surface_destroy(struct wlr_xdg_surface_v6 *surface) {
	if (surface->role != WLR_XDG_SURFACE_V6_ROLE_NONE) {
		xdg_surface_unmap(surface);
	}

	wlr_signal_emit_safe(&surface->events.destroy, surface);

	wl_resource_set_user_data(surface->resource, NULL);
	wl_list_remove(&surface->link);
	wl_list_remove(&surface->surface_destroy_listener.link);
	wlr_surface_set_role_committed(surface->surface, NULL, NULL);
	free(surface->geometry);
	free(surface->next_geometry);
	free(surface);
}


static const struct zxdg_positioner_v6_interface
	zxdg_positioner_v6_implementation;

static struct wlr_xdg_positioner_v6 *xdg_positioner_from_resource(
		struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource, &zxdg_positioner_v6_interface,
		&zxdg_positioner_v6_implementation));
	return wl_resource_get_user_data(resource);
}

static void xdg_positioner_destroy(struct wl_resource *resource) {
	struct wlr_xdg_positioner_v6 *positioner =
		xdg_positioner_from_resource(resource);
	free(positioner);
}

static void xdg_positioner_handle_set_size(struct wl_client *client,
		struct wl_resource *resource, int32_t width, int32_t height) {
	struct wlr_xdg_positioner_v6 *positioner =
		xdg_positioner_from_resource(resource);

	if (width < 1 || height < 1) {
		wl_resource_post_error(resource,
			ZXDG_POSITIONER_V6_ERROR_INVALID_INPUT,
			"width and height must be positive and non-zero");
		return;
	}

	positioner->size.width = width;
	positioner->size.height = height;
}

static void xdg_positioner_handle_set_anchor_rect(struct wl_client *client,
		struct wl_resource *resource, int32_t x, int32_t y, int32_t width,
		int32_t height) {
	struct wlr_xdg_positioner_v6 *positioner =
		xdg_positioner_from_resource(resource);

	if (width < 1 || height < 1) {
		wl_resource_post_error(resource,
			ZXDG_POSITIONER_V6_ERROR_INVALID_INPUT,
			"width and height must be positive and non-zero");
		return;
	}

	positioner->anchor_rect.x = x;
	positioner->anchor_rect.y = y;
	positioner->anchor_rect.width = width;
	positioner->anchor_rect.height = height;
}

static void xdg_positioner_handle_set_anchor(struct wl_client *client,
		struct wl_resource *resource, uint32_t anchor) {
	struct wlr_xdg_positioner_v6 *positioner =
		xdg_positioner_from_resource(resource);

	if (((anchor & ZXDG_POSITIONER_V6_ANCHOR_TOP ) &&
				(anchor & ZXDG_POSITIONER_V6_ANCHOR_BOTTOM)) ||
			((anchor & ZXDG_POSITIONER_V6_ANCHOR_LEFT) &&
				(anchor & ZXDG_POSITIONER_V6_ANCHOR_RIGHT))) {
		wl_resource_post_error(resource,
			ZXDG_POSITIONER_V6_ERROR_INVALID_INPUT,
			"same-axis values are not allowed");
		return;
	}

	positioner->anchor = anchor;
}

static void xdg_positioner_handle_set_gravity(struct wl_client *client,
		struct wl_resource *resource, uint32_t gravity) {
	struct wlr_xdg_positioner_v6 *positioner =
		xdg_positioner_from_resource(resource);

	if (((gravity & ZXDG_POSITIONER_V6_GRAVITY_TOP) &&
				(gravity & ZXDG_POSITIONER_V6_GRAVITY_BOTTOM)) ||
			((gravity & ZXDG_POSITIONER_V6_GRAVITY_LEFT) &&
				(gravity & ZXDG_POSITIONER_V6_GRAVITY_RIGHT))) {
		wl_resource_post_error(resource,
			ZXDG_POSITIONER_V6_ERROR_INVALID_INPUT,
			"same-axis values are not allowed");
		return;
	}

	positioner->gravity = gravity;
}

static void xdg_positioner_handle_set_constraint_adjustment(
		struct wl_client *client, struct wl_resource *resource,
		uint32_t constraint_adjustment) {
	struct wlr_xdg_positioner_v6 *positioner =
		xdg_positioner_from_resource(resource);

	positioner->constraint_adjustment = constraint_adjustment;
}

static void xdg_positioner_handle_set_offset(struct wl_client *client,
		struct wl_resource *resource, int32_t x, int32_t y) {
	struct wlr_xdg_positioner_v6 *positioner =
		xdg_positioner_from_resource(resource);

	positioner->offset.x = x;
	positioner->offset.y = y;
}

static const struct zxdg_positioner_v6_interface
		zxdg_positioner_v6_implementation = {
	.destroy = resource_handle_destroy,
	.set_size = xdg_positioner_handle_set_size,
	.set_anchor_rect = xdg_positioner_handle_set_anchor_rect,
	.set_anchor = xdg_positioner_handle_set_anchor,
	.set_gravity = xdg_positioner_handle_set_gravity,
	.set_constraint_adjustment =
		xdg_positioner_handle_set_constraint_adjustment,
	.set_offset = xdg_positioner_handle_set_offset,
};

static void xdg_shell_handle_create_positioner(struct wl_client *wl_client,
		struct wl_resource *resource, uint32_t id) {
	struct wlr_xdg_positioner_v6 *positioner =
		calloc(1, sizeof(struct wlr_xdg_positioner_v6));
	if (positioner == NULL) {
		wl_client_post_no_memory(wl_client);
		return;
	}

	positioner->resource = wl_resource_create(wl_client,
		&zxdg_positioner_v6_interface,
		wl_resource_get_version(resource),
		id);
	if (positioner->resource == NULL) {
		free(positioner);
		wl_client_post_no_memory(wl_client);
		return;
	}

	wl_resource_set_implementation(positioner->resource,
		&zxdg_positioner_v6_implementation,
		positioner, xdg_positioner_destroy);
}

static struct wlr_box xdg_positioner_get_geometry(
		struct wlr_xdg_positioner_v6 *positioner,
		struct wlr_xdg_surface_v6 *surface, struct wlr_xdg_surface_v6 *parent) {
	struct wlr_box geometry = {
		.x = positioner->offset.x,
		.y = positioner->offset.y,
		.width = positioner->size.width,
		.height = positioner->size.height,
	};

	if (positioner->anchor & ZXDG_POSITIONER_V6_ANCHOR_TOP) {
		geometry.y += positioner->anchor_rect.y;
	} else if (positioner->anchor & ZXDG_POSITIONER_V6_ANCHOR_BOTTOM) {
		geometry.y +=
			positioner->anchor_rect.y + positioner->anchor_rect.height;
	} else {
		geometry.y +=
			positioner->anchor_rect.y + positioner->anchor_rect.height / 2;
	}

	if (positioner->anchor & ZXDG_POSITIONER_V6_ANCHOR_LEFT) {
		geometry.x += positioner->anchor_rect.x;
	} else if (positioner->anchor & ZXDG_POSITIONER_V6_ANCHOR_RIGHT) {
		geometry.x += positioner->anchor_rect.x + positioner->anchor_rect.width;
	} else {
		geometry.x +=
			positioner->anchor_rect.x + positioner->anchor_rect.width / 2;
	}

	if (positioner->gravity & ZXDG_POSITIONER_V6_GRAVITY_TOP) {
		geometry.y -= geometry.height;
	} else if (positioner->gravity & ZXDG_POSITIONER_V6_GRAVITY_BOTTOM) {
		geometry.y = geometry.y;
	} else {
		geometry.y -= geometry.height / 2;
	}

	if (positioner->gravity & ZXDG_POSITIONER_V6_GRAVITY_LEFT) {
		geometry.x -= geometry.width;
	} else if (positioner->gravity & ZXDG_POSITIONER_V6_GRAVITY_RIGHT) {
		geometry.x = geometry.x;
	} else {
		geometry.x -= geometry.width / 2;
	}

	if (positioner->constraint_adjustment ==
			ZXDG_POSITIONER_V6_CONSTRAINT_ADJUSTMENT_NONE) {
		return geometry;
	}

	// TODO: add compositor policy configuration and the code here

	return geometry;
}


static const struct zxdg_popup_v6_interface zxdg_popup_v6_implementation;

static struct wlr_xdg_surface_v6 *xdg_surface_from_xdg_popup_resource(
		struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource, &zxdg_popup_v6_interface,
		&zxdg_popup_v6_implementation));
	return wl_resource_get_user_data(resource);
}

static void xdg_popup_handle_grab(struct wl_client *client,
		struct wl_resource *resource, struct wl_resource *seat_resource,
		uint32_t serial) {
	struct wlr_xdg_surface_v6 *surface =
		xdg_surface_from_xdg_popup_resource(resource);
	struct wlr_seat_client *seat_client =
		wlr_seat_client_from_resource(seat_resource);

	if (surface->popup_state->committed) {
		wl_resource_post_error(surface->popup_state->resource,
			ZXDG_POPUP_V6_ERROR_INVALID_GRAB,
			"xdg_popup is already mapped");
		return;
	}

	struct wlr_xdg_popup_grab_v6 *popup_grab =
		xdg_shell_popup_grab_from_seat(surface->client->shell,
			seat_client->seat);

	struct wlr_xdg_surface_v6 *topmost = xdg_popup_grab_get_topmost(popup_grab);
	bool parent_is_toplevel =
		surface->popup_state->parent->role == WLR_XDG_SURFACE_V6_ROLE_TOPLEVEL;

	if ((topmost == NULL && !parent_is_toplevel) ||
			(topmost != NULL && topmost != surface->popup_state->parent)) {
		wl_resource_post_error(surface->client->resource,
			ZXDG_SHELL_V6_ERROR_NOT_THE_TOPMOST_POPUP,
			"xdg_popup was not created on the topmost popup");
		return;
	}

	popup_grab->client = surface->client->client;
	surface->popup_state->seat = seat_client->seat;

	wl_list_insert(&popup_grab->popups, &surface->popup_state->grab_link);

	wlr_seat_pointer_start_grab(seat_client->seat,
		&popup_grab->pointer_grab);
	wlr_seat_keyboard_start_grab(seat_client->seat,
		&popup_grab->keyboard_grab);
}

static void xdg_popup_handle_destroy(struct wl_client *client,
		struct wl_resource *resource) {
	struct wlr_xdg_surface_v6 *surface =
		xdg_surface_from_xdg_popup_resource(resource);
	struct wlr_xdg_popup_grab_v6 *grab =
		xdg_shell_popup_grab_from_seat(surface->client->shell,
			surface->popup_state->seat);
	struct wlr_xdg_surface_v6 *topmost =
		xdg_popup_grab_get_topmost(grab);

	if (topmost != surface) {
		wl_resource_post_error(surface->client->resource,
			ZXDG_SHELL_V6_ERROR_NOT_THE_TOPMOST_POPUP,
			"xdg_popup was destroyed while it was not the topmost "
			"popup");
		return;
	}

	wl_resource_destroy(resource);
}

static const struct zxdg_popup_v6_interface zxdg_popup_v6_implementation = {
	.destroy = xdg_popup_handle_destroy,
	.grab = xdg_popup_handle_grab,
};

static void xdg_popup_resource_destroy(struct wl_resource *resource) {
	struct wlr_xdg_surface_v6 *surface =
		xdg_surface_from_xdg_popup_resource(resource);
	if (surface != NULL) {
		xdg_surface_unmap(surface);
	}
}

static const struct zxdg_surface_v6_interface zxdg_surface_v6_implementation;

static struct wlr_xdg_surface_v6 *xdg_surface_from_resource(
		struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource, &zxdg_surface_v6_interface,
		&zxdg_surface_v6_implementation));
	return wl_resource_get_user_data(resource);
}

static void xdg_surface_handle_get_popup(struct wl_client *client,
		struct wl_resource *resource, uint32_t id,
		struct wl_resource *parent_resource,
		struct wl_resource *positioner_resource) {
	struct wlr_xdg_surface_v6 *surface =
		xdg_surface_from_resource(resource);
	struct wlr_xdg_surface_v6 *parent =
		xdg_surface_from_resource(parent_resource);
	struct wlr_xdg_positioner_v6 *positioner =
		xdg_positioner_from_resource(positioner_resource);

	if (positioner->size.width == 0 || positioner->anchor_rect.width == 0) {
		wl_resource_post_error(resource,
			ZXDG_SHELL_V6_ERROR_INVALID_POSITIONER,
			"positioner object is not complete");
		return;
	}

	if (wlr_surface_set_role(surface->surface, wlr_desktop_xdg_popup_role,
			resource, ZXDG_SHELL_V6_ERROR_ROLE)) {
		return;
	}

	surface->popup_state = calloc(1, sizeof(struct wlr_xdg_popup_v6));
	if (!surface->popup_state) {
		wl_resource_post_no_memory(resource);
		return;
	}

	surface->popup_state->resource =
		wl_resource_create(client, &zxdg_popup_v6_interface,
			wl_resource_get_version(resource), id);
	if (surface->popup_state->resource == NULL) {
		free(surface->popup_state);
		wl_resource_post_no_memory(resource);
		return;
	}

	surface->role = WLR_XDG_SURFACE_V6_ROLE_POPUP;
	surface->popup_state->base = surface;
	surface->popup_state->parent = parent;
	surface->popup_state->geometry =
		xdg_positioner_get_geometry(positioner, surface, parent);
	wl_list_insert(&parent->popups, &surface->popup_state->link);

	wl_resource_set_implementation(surface->popup_state->resource,
		&zxdg_popup_v6_implementation, surface,
		xdg_popup_resource_destroy);

	wlr_signal_emit_safe(&parent->events.new_popup, surface->popup_state);
}


static const struct zxdg_toplevel_v6_interface zxdg_toplevel_v6_implementation;

static struct wlr_xdg_surface_v6 *xdg_surface_from_xdg_toplevel_resource(
		struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource, &zxdg_toplevel_v6_interface,
		&zxdg_toplevel_v6_implementation));
	return wl_resource_get_user_data(resource);
}

static void xdg_toplevel_handle_set_parent(struct wl_client *client,
		struct wl_resource *resource, struct wl_resource *parent_resource) {
	struct wlr_xdg_surface_v6 *surface =
		xdg_surface_from_xdg_toplevel_resource(resource);

	struct wlr_xdg_surface_v6 *parent = NULL;
	if (parent_resource != NULL) {
		parent = xdg_surface_from_xdg_toplevel_resource(parent_resource);
	}

	surface->toplevel_state->parent = parent;
}

static void xdg_toplevel_handle_set_title(struct wl_client *client,
		struct wl_resource *resource, const char *title) {
	struct wlr_xdg_surface_v6 *surface =
		xdg_surface_from_xdg_toplevel_resource(resource);

	char *tmp = strdup(title);
	if (tmp == NULL) {
		return;
	}

	free(surface->title);
	surface->title = tmp;
}

static void xdg_toplevel_handle_set_app_id(struct wl_client *client,
		struct wl_resource *resource, const char *app_id) {
	struct wlr_xdg_surface_v6 *surface =
		xdg_surface_from_xdg_toplevel_resource(resource);

	char *tmp = strdup(app_id);
	if (tmp == NULL) {
		return;
	}

	free(surface->app_id);
	surface->app_id = tmp;
}

static void xdg_toplevel_handle_show_window_menu(struct wl_client *client,
		struct wl_resource *resource, struct wl_resource *seat_resource,
		uint32_t serial, int32_t x, int32_t y) {
	struct wlr_xdg_surface_v6 *surface =
		xdg_surface_from_xdg_toplevel_resource(resource);
	struct wlr_seat_client *seat =
		wlr_seat_client_from_resource(seat_resource);

	if (!surface->configured) {
		wl_resource_post_error(surface->toplevel_state->resource,
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

	wlr_signal_emit_safe(&surface->events.request_show_window_menu, &event);
}

static void xdg_toplevel_handle_move(struct wl_client *client,
		struct wl_resource *resource, struct wl_resource *seat_resource,
		uint32_t serial) {
	struct wlr_xdg_surface_v6 *surface =
		xdg_surface_from_xdg_toplevel_resource(resource);
	struct wlr_seat_client *seat =
		wlr_seat_client_from_resource(seat_resource);

	if (!surface->configured) {
		wl_resource_post_error(surface->toplevel_state->resource,
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

	wlr_signal_emit_safe(&surface->events.request_move, &event);
}

static void xdg_toplevel_handle_resize(struct wl_client *client,
		struct wl_resource *resource, struct wl_resource *seat_resource,
		uint32_t serial, uint32_t edges) {
	struct wlr_xdg_surface_v6 *surface =
		xdg_surface_from_xdg_toplevel_resource(resource);
	struct wlr_seat_client *seat =
		wlr_seat_client_from_resource(seat_resource);

	if (!surface->configured) {
		wl_resource_post_error(surface->toplevel_state->resource,
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

	wlr_signal_emit_safe(&surface->events.request_resize, &event);
}

static void xdg_toplevel_handle_set_max_size(struct wl_client *client,
		struct wl_resource *resource, int32_t width, int32_t height) {
	struct wlr_xdg_surface_v6 *surface =
		xdg_surface_from_xdg_toplevel_resource(resource);
	surface->toplevel_state->next.max_width = width;
	surface->toplevel_state->next.max_height = height;
}

static void xdg_toplevel_handle_set_min_size(struct wl_client *client,
		struct wl_resource *resource, int32_t width, int32_t height) {
	struct wlr_xdg_surface_v6 *surface =
		xdg_surface_from_xdg_toplevel_resource(resource);
	surface->toplevel_state->next.min_width = width;
	surface->toplevel_state->next.min_height = height;
}

static void xdg_toplevel_handle_set_maximized(struct wl_client *client,
		struct wl_resource *resource) {
	struct wlr_xdg_surface_v6 *surface =
		xdg_surface_from_xdg_toplevel_resource(resource);
	surface->toplevel_state->next.maximized = true;
	wlr_signal_emit_safe(&surface->events.request_maximize, surface);
}

static void xdg_toplevel_handle_unset_maximized(struct wl_client *client,
		struct wl_resource *resource) {
	struct wlr_xdg_surface_v6 *surface =
		xdg_surface_from_xdg_toplevel_resource(resource);
	surface->toplevel_state->next.maximized = false;
	wlr_signal_emit_safe(&surface->events.request_maximize, surface);
}

static void xdg_toplevel_handle_set_fullscreen(struct wl_client *client,
		struct wl_resource *resource, struct wl_resource *output_resource) {
	struct wlr_xdg_surface_v6 *surface =
		xdg_surface_from_xdg_toplevel_resource(resource);

	struct wlr_output *output = NULL;
	if (output_resource != NULL) {
		output = wlr_output_from_resource(output_resource);
	}

	surface->toplevel_state->next.fullscreen = true;

	struct wlr_xdg_toplevel_v6_set_fullscreen_event event = {
		.surface = surface,
		.fullscreen = true,
		.output = output,
	};

	wlr_signal_emit_safe(&surface->events.request_fullscreen, &event);
}

static void xdg_toplevel_handle_unset_fullscreen(struct wl_client *client,
		struct wl_resource *resource) {
	struct wlr_xdg_surface_v6 *surface =
		xdg_surface_from_xdg_toplevel_resource(resource);

	surface->toplevel_state->next.fullscreen = false;

	struct wlr_xdg_toplevel_v6_set_fullscreen_event event = {
		.surface = surface,
		.fullscreen = false,
		.output = NULL,
	};

	wlr_signal_emit_safe(&surface->events.request_fullscreen, &event);
}

static void xdg_toplevel_handle_set_minimized(struct wl_client *client,
		struct wl_resource *resource) {
	struct wlr_xdg_surface_v6 *surface =
		xdg_surface_from_xdg_toplevel_resource(resource);
	wlr_signal_emit_safe(&surface->events.request_minimize, surface);
}

static const struct zxdg_toplevel_v6_interface zxdg_toplevel_v6_implementation =
{
	.destroy = resource_handle_destroy,
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
	.set_minimized = xdg_toplevel_handle_set_minimized
};

static void xdg_surface_resource_destroy(struct wl_resource *resource) {
	struct wlr_xdg_surface_v6 *surface = xdg_surface_from_resource(resource);
	if (surface != NULL) {
		xdg_surface_destroy(surface);
	}
}

static void xdg_toplevel_resource_destroy(struct wl_resource *resource) {
	struct wlr_xdg_surface_v6 *surface =
		xdg_surface_from_xdg_toplevel_resource(resource);
	if (surface != NULL) {
		xdg_surface_unmap(surface);
	}
}

static void xdg_surface_handle_get_toplevel(struct wl_client *client,
		struct wl_resource *resource, uint32_t id) {
	struct wlr_xdg_surface_v6 *surface = xdg_surface_from_resource(resource);

	if (wlr_surface_set_role(surface->surface, wlr_desktop_xdg_toplevel_role,
			resource, ZXDG_SHELL_V6_ERROR_ROLE)) {
		return;
	}

	surface->toplevel_state = calloc(1, sizeof(struct wlr_xdg_toplevel_v6));
	if (surface->toplevel_state == NULL) {
		wl_resource_post_no_memory(resource);
		return;
	}

	surface->role = WLR_XDG_SURFACE_V6_ROLE_TOPLEVEL;
	surface->toplevel_state->base = surface;

	struct wl_resource *toplevel_resource = wl_resource_create(client,
		&zxdg_toplevel_v6_interface, wl_resource_get_version(resource), id);
	if (toplevel_resource == NULL) {
		free(surface->toplevel_state);
		wl_resource_post_no_memory(resource);
		return;
	}

	surface->toplevel_state->resource = toplevel_resource;

	wl_resource_set_implementation(toplevel_resource,
		&zxdg_toplevel_v6_implementation, surface,
		xdg_toplevel_resource_destroy);
}

static void wlr_xdg_toplevel_v6_ack_configure(
		struct wlr_xdg_surface_v6 *surface,
		struct wlr_xdg_surface_v6_configure *configure) {
	assert(surface->role == WLR_XDG_SURFACE_V6_ROLE_TOPLEVEL);
	assert(configure->toplevel_state != NULL);

	surface->toplevel_state->current.maximized =
		configure->toplevel_state->maximized;
	surface->toplevel_state->current.fullscreen =
		configure->toplevel_state->fullscreen;
	surface->toplevel_state->current.resizing =
		configure->toplevel_state->resizing;
	surface->toplevel_state->current.activated =
		configure->toplevel_state->activated;

	free(configure->toplevel_state);
	configure->toplevel_state = NULL;
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
		wlr_xdg_toplevel_v6_ack_configure(surface, configure);
		break;
	case WLR_XDG_SURFACE_V6_ROLE_POPUP:
		break;
	}

	surface->configured = true;
	surface->configure_serial = serial;

	free(configure);
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

	surface->has_next_geometry = true;
	surface->next_geometry->height = height;
	surface->next_geometry->width = width;
	surface->next_geometry->x = x;
	surface->next_geometry->y = y;
}

static void xdg_surface_handle_destroy(struct wl_client *client,
		struct wl_resource *resource) {
	struct wlr_xdg_surface_v6 *surface = xdg_surface_from_resource(resource);

	if (surface->role != WLR_XDG_SURFACE_V6_ROLE_NONE) {
		wlr_log(L_ERROR, "Tried to destroy an xdg_surface before its role "
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

static bool wlr_xdg_surface_v6_toplevel_state_compare(
		struct wlr_xdg_toplevel_v6 *state) {
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

	if (state->pending.activated != configured.state.activated) {
		return false;
	}
	if (state->pending.fullscreen != configured.state.fullscreen) {
		return false;
	}
	if (state->pending.maximized != configured.state.maximized) {
		return false;
	}
	if (state->pending.resizing != configured.state.resizing) {
		return false;
	}

	if (state->pending.width == configured.width &&
			state->pending.height == configured.height) {
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

	configure->toplevel_state = malloc(sizeof(*configure->toplevel_state));
	if (configure->toplevel_state == NULL) {
		wlr_log(L_ERROR, "Allocation failed");
		return;
	}
	*configure->toplevel_state = surface->toplevel_state->pending;

	wl_array_init(&states);
	if (surface->toplevel_state->pending.maximized) {
		s = wl_array_add(&states, sizeof(uint32_t));
		if (!s) {
			wlr_log(L_ERROR, "Could not allocate state for maximized xdg_toplevel");
			goto error_out;
		}
		*s = ZXDG_TOPLEVEL_V6_STATE_MAXIMIZED;
	}
	if (surface->toplevel_state->pending.fullscreen) {
		s = wl_array_add(&states, sizeof(uint32_t));
		if (!s) {
			wlr_log(L_ERROR, "Could not allocate state for fullscreen xdg_toplevel");
			goto error_out;
		}
		*s = ZXDG_TOPLEVEL_V6_STATE_FULLSCREEN;
	}
	if (surface->toplevel_state->pending.resizing) {
		s = wl_array_add(&states, sizeof(uint32_t));
		if (!s) {
			wlr_log(L_ERROR, "Could not allocate state for resizing xdg_toplevel");
			goto error_out;
		}
		*s = ZXDG_TOPLEVEL_V6_STATE_RESIZING;
	}
	if (surface->toplevel_state->pending.activated) {
		s = wl_array_add(&states, sizeof(uint32_t));
		if (!s) {
			wlr_log(L_ERROR, "Could not allocate state for activated xdg_toplevel");
			goto error_out;
		}
		*s = ZXDG_TOPLEVEL_V6_STATE_ACTIVATED;
	}

	uint32_t width = surface->toplevel_state->pending.width;
	uint32_t height = surface->toplevel_state->pending.height;

	if (width == 0 || height == 0) {
		width = surface->geometry->width;
		height = surface->geometry->height;
	}

	zxdg_toplevel_v6_send_configure(surface->toplevel_state->resource, width,
		height, &states);

	wl_array_release(&states);
	return;

error_out:
	wl_array_release(&states);
	wl_resource_post_no_memory(surface->toplevel_state->resource);
}

static void wlr_xdg_surface_send_configure(void *user_data) {
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
		wlr_xdg_toplevel_v6_send_configure(surface, configure);
		break;
	case WLR_XDG_SURFACE_V6_ROLE_POPUP:
		zxdg_popup_v6_send_configure(surface->popup_state->resource,
			surface->popup_state->geometry.x,
			surface->popup_state->geometry.y,
			surface->popup_state->geometry.width,
			surface->popup_state->geometry.height);
		break;
	}

	zxdg_surface_v6_send_configure(surface->resource, configure->serial);
}

static uint32_t wlr_xdg_surface_v6_schedule_configure(
		struct wlr_xdg_surface_v6 *surface) {
	struct wl_display *display = wl_client_get_display(surface->client->client);
	struct wl_event_loop *loop = wl_display_get_event_loop(display);
	bool pending_same = false;

	switch (surface->role) {
	case WLR_XDG_SURFACE_V6_ROLE_NONE:
		assert(0 && "not reached");
		break;
	case WLR_XDG_SURFACE_V6_ROLE_TOPLEVEL:
		pending_same =
			wlr_xdg_surface_v6_toplevel_state_compare(surface->toplevel_state);
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
			wlr_xdg_surface_send_configure, surface);
		return surface->configure_next_serial;
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
	assert(surface->role == WLR_XDG_SURFACE_V6_ROLE_TOPLEVEL);

	if (!surface->toplevel_state->added) {
		// on the first commit, send a configure request to tell the client it
		// is added
		wlr_xdg_surface_v6_schedule_configure(surface);
		surface->toplevel_state->added = true;
		return;
	}

	// update state that doesn't need compositor approval
	surface->toplevel_state->current.max_width =
		surface->toplevel_state->next.max_width;
	surface->toplevel_state->current.min_width =
		surface->toplevel_state->next.min_width;
	surface->toplevel_state->current.max_height =
		surface->toplevel_state->next.max_height;
	surface->toplevel_state->current.min_height =
		surface->toplevel_state->next.min_height;
}

static void wlr_xdg_surface_v6_popup_committed(
		struct wlr_xdg_surface_v6 *surface) {
	assert(surface->role == WLR_XDG_SURFACE_V6_ROLE_POPUP);

	if (!surface->popup_state->committed) {
		wlr_xdg_surface_v6_schedule_configure(surface);
		surface->popup_state->committed = true;
	}
}

static void handle_wlr_surface_committed(struct wlr_surface *wlr_surface,
		void *role_data) {
	struct wlr_xdg_surface_v6 *surface = role_data;

	if (wlr_surface_has_buffer(surface->surface) && !surface->configured) {
		wl_resource_post_error(surface->resource,
			ZXDG_SURFACE_V6_ERROR_UNCONFIGURED_BUFFER,
			"xdg_surface has never been configured");
		return;
	}

	if (surface->has_next_geometry) {
		surface->has_next_geometry = false;
		surface->geometry->x = surface->next_geometry->x;
		surface->geometry->y = surface->next_geometry->y;
		surface->geometry->width = surface->next_geometry->width;
		surface->geometry->height = surface->next_geometry->height;
	}

	switch (surface->role) {
	case WLR_XDG_SURFACE_V6_ROLE_NONE:
		wl_resource_post_error(surface->resource,
			ZXDG_SURFACE_V6_ERROR_NOT_CONSTRUCTED,
			"xdg_surface must have a role");
		break;
	case WLR_XDG_SURFACE_V6_ROLE_TOPLEVEL:
		wlr_xdg_surface_v6_toplevel_committed(surface);
		break;
	case WLR_XDG_SURFACE_V6_ROLE_POPUP:
		wlr_xdg_surface_v6_popup_committed(surface);
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
		xdg_surface_unmap(surface);
	}
}

static const struct zxdg_shell_v6_interface xdg_shell_impl;

static struct wlr_xdg_client_v6 *xdg_client_from_resource(
		struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource, &zxdg_shell_v6_interface,
		&xdg_shell_impl));
	return wl_resource_get_user_data(resource);
}

static void xdg_shell_handle_get_xdg_surface(struct wl_client *wl_client,
		struct wl_resource *client_resource, uint32_t id,
		struct wl_resource *surface_resource) {
	struct wlr_xdg_client_v6 *client =
		xdg_client_from_resource(client_resource);

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
	surface->surface = wlr_surface_from_resource(surface_resource);
	surface->resource = wl_resource_create(wl_client,
		&zxdg_surface_v6_interface, wl_resource_get_version(client_resource),
		id);
	if (surface->resource == NULL) {
		free(surface->next_geometry);
		free(surface->geometry);
		free(surface);
		wl_client_post_no_memory(wl_client);
		return;
	}

	if (wlr_surface_has_buffer(surface->surface)) {
		wl_resource_destroy(surface->resource);
		free(surface->next_geometry);
		free(surface->geometry);
		free(surface);
		wl_resource_post_error(surface_resource,
			ZXDG_SURFACE_V6_ERROR_UNCONFIGURED_BUFFER,
			"xdg_surface must not have a buffer at creation");
		return;
	}

	wl_list_init(&surface->configure_list);
	wl_list_init(&surface->popups);

	wl_signal_init(&surface->events.request_maximize);
	wl_signal_init(&surface->events.request_fullscreen);
	wl_signal_init(&surface->events.request_minimize);
	wl_signal_init(&surface->events.request_move);
	wl_signal_init(&surface->events.request_resize);
	wl_signal_init(&surface->events.request_show_window_menu);
	wl_signal_init(&surface->events.destroy);
	wl_signal_init(&surface->events.ping_timeout);
	wl_signal_init(&surface->events.new_popup);
	wl_signal_init(&surface->events.map);
	wl_signal_init(&surface->events.unmap);

	wl_signal_add(&surface->surface->events.destroy,
		&surface->surface_destroy_listener);
	surface->surface_destroy_listener.notify = handle_wlr_surface_destroyed;

	wlr_surface_set_role_committed(surface->surface,
		handle_wlr_surface_committed, surface);

	wlr_log(L_DEBUG, "new xdg_surface %p (res %p)", surface, surface->resource);
	wl_resource_set_implementation(surface->resource,
		&zxdg_surface_v6_implementation, surface, xdg_surface_resource_destroy);
	wl_list_insert(&client->surfaces, &surface->link);
}

static void xdg_shell_handle_pong(struct wl_client *wl_client,
		struct wl_resource *resource, uint32_t serial) {
	struct wlr_xdg_client_v6 *client = xdg_client_from_resource(resource);

	if (client->ping_serial != serial) {
		return;
	}

	wl_event_source_timer_update(client->ping_timer, 0);
	client->ping_serial = 0;
}

static void xdg_shell_handle_destroy(struct wl_client *wl_client,
		struct wl_resource *resource) {
	struct wlr_xdg_client_v6 *client = xdg_client_from_resource(resource);

	if (!wl_list_empty(&client->surfaces)) {
		wl_resource_post_error(client->resource,
			ZXDG_SHELL_V6_ERROR_DEFUNCT_SURFACES,
			"xdg_wm_base was destroyed before children");
		return;
	}

	wl_resource_destroy(resource);
}

static const struct zxdg_shell_v6_interface xdg_shell_impl = {
	.destroy = xdg_shell_handle_destroy,
	.create_positioner = xdg_shell_handle_create_positioner,
	.get_xdg_surface = xdg_shell_handle_get_xdg_surface,
	.pong = xdg_shell_handle_pong,
};

static void wlr_xdg_client_v6_destroy(struct wl_resource *resource) {
	struct wlr_xdg_client_v6 *client = xdg_client_from_resource(resource);

	struct wlr_xdg_surface_v6 *surface, *tmp = NULL;
	wl_list_for_each_safe(surface, tmp, &client->surfaces, link) {
		xdg_surface_destroy(surface);
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
		wlr_signal_emit_safe(&surface->events.ping_timeout, surface);
	}

	client->ping_serial = 0;
	return 1;
}

static void xdg_shell_bind(struct wl_client *wl_client, void *data,
		uint32_t version, uint32_t id) {
	struct wlr_xdg_shell_v6 *xdg_shell = data;
	assert(wl_client && xdg_shell);

	struct wlr_xdg_client_v6 *client =
		calloc(1, sizeof(struct wlr_xdg_client_v6));
	if (client == NULL) {
		wl_client_post_no_memory(wl_client);
		return;
	}

	wl_list_init(&client->surfaces);

	client->resource =
		wl_resource_create(wl_client, &zxdg_shell_v6_interface, version, id);
	if (client->resource == NULL) {
		free(client);
		wl_client_post_no_memory(wl_client);
		return;
	}
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

static void handle_display_destroy(struct wl_listener *listener, void *data) {
	struct wlr_xdg_shell_v6 *xdg_shell =
		wl_container_of(listener, xdg_shell, display_destroy);
	wlr_xdg_shell_v6_destroy(xdg_shell);
}

struct wlr_xdg_shell_v6 *wlr_xdg_shell_v6_create(struct wl_display *display) {
	struct wlr_xdg_shell_v6 *xdg_shell =
		calloc(1, sizeof(struct wlr_xdg_shell_v6));
	if (!xdg_shell) {
		return NULL;
	}

	xdg_shell->ping_timeout = 10000;

	wl_list_init(&xdg_shell->clients);
	wl_list_init(&xdg_shell->popup_grabs);

	struct wl_global *wl_global = wl_global_create(display,
		&zxdg_shell_v6_interface, 1, xdg_shell, xdg_shell_bind);
	if (!wl_global) {
		free(xdg_shell);
		return NULL;
	}
	xdg_shell->wl_global = wl_global;

	wl_signal_init(&xdg_shell->events.new_surface);

	xdg_shell->display_destroy.notify = handle_display_destroy;
	wl_display_add_destroy_listener(display, &xdg_shell->display_destroy);

	return xdg_shell;
}

void wlr_xdg_shell_v6_destroy(struct wlr_xdg_shell_v6 *xdg_shell) {
	if (!xdg_shell) {
		return;
	}
	wl_list_remove(&xdg_shell->display_destroy.link);
	wl_global_destroy(xdg_shell->wl_global);
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

uint32_t wlr_xdg_toplevel_v6_set_size(struct wlr_xdg_surface_v6 *surface,
		uint32_t width, uint32_t height) {
	assert(surface->role == WLR_XDG_SURFACE_V6_ROLE_TOPLEVEL);
	surface->toplevel_state->pending.width = width;
	surface->toplevel_state->pending.height = height;
	wlr_log(L_DEBUG, "wlr_xdg_toplevel_v6_set_size %d", width);

	return wlr_xdg_surface_v6_schedule_configure(surface);
}

uint32_t wlr_xdg_toplevel_v6_set_activated(struct wlr_xdg_surface_v6 *surface,
		bool activated) {
	assert(surface->role == WLR_XDG_SURFACE_V6_ROLE_TOPLEVEL);
	surface->toplevel_state->pending.activated = activated;

	return wlr_xdg_surface_v6_schedule_configure(surface);
}

uint32_t wlr_xdg_toplevel_v6_set_maximized(struct wlr_xdg_surface_v6 *surface,
		bool maximized) {
	assert(surface->role == WLR_XDG_SURFACE_V6_ROLE_TOPLEVEL);
	surface->toplevel_state->pending.maximized = maximized;

	return wlr_xdg_surface_v6_schedule_configure(surface);
}

uint32_t wlr_xdg_toplevel_v6_set_fullscreen(struct wlr_xdg_surface_v6 *surface,
		bool fullscreen) {
	assert(surface->role == WLR_XDG_SURFACE_V6_ROLE_TOPLEVEL);
	surface->toplevel_state->pending.fullscreen = fullscreen;

	return wlr_xdg_surface_v6_schedule_configure(surface);
}

uint32_t wlr_xdg_toplevel_v6_set_resizing(struct wlr_xdg_surface_v6 *surface,
		bool resizing) {
	assert(surface->role == WLR_XDG_SURFACE_V6_ROLE_TOPLEVEL);
	surface->toplevel_state->pending.resizing = resizing;

	return wlr_xdg_surface_v6_schedule_configure(surface);
}

void wlr_xdg_toplevel_v6_send_close(struct wlr_xdg_surface_v6 *surface) {
	assert(surface->role == WLR_XDG_SURFACE_V6_ROLE_TOPLEVEL);
	zxdg_toplevel_v6_send_close(surface->toplevel_state->resource);
}

void wlr_xdg_surface_v6_popup_get_position(struct wlr_xdg_surface_v6 *surface,
		double *popup_sx, double *popup_sy) {
	assert(surface->role == WLR_XDG_SURFACE_V6_ROLE_POPUP);
	struct wlr_xdg_surface_v6 *parent = surface->popup_state->parent;
	*popup_sx = parent->geometry->x + surface->popup_state->geometry.x -
		surface->geometry->x;
	*popup_sy = parent->geometry->y + surface->popup_state->geometry.y -
		surface->geometry->y;
}

struct wlr_xdg_surface_v6 *wlr_xdg_surface_v6_popup_at(
		struct wlr_xdg_surface_v6 *surface, double sx, double sy,
		double *popup_sx, double *popup_sy) {
	// XXX: I think this is so complicated because we're mixing geometry
	// coordinates with surface coordinates. Input handling should only deal
	// with surface coordinates.
	struct wlr_xdg_popup_v6 *popup_state;
	wl_list_for_each(popup_state, &surface->popups, link) {
		struct wlr_xdg_surface_v6 *popup = popup_state->base;

		double _popup_sx =
			surface->geometry->x + popup_state->geometry.x;
		double _popup_sy =
			surface->geometry->y + popup_state->geometry.y;
		int popup_width =  popup_state->geometry.width;
		int popup_height =  popup_state->geometry.height;

		struct wlr_xdg_surface_v6 *_popup =
			wlr_xdg_surface_v6_popup_at(popup,
				sx - _popup_sx + popup->geometry->x,
				sy - _popup_sy + popup->geometry->y,
				popup_sx, popup_sy);
		if (_popup) {
			*popup_sx = *popup_sx + _popup_sx - popup->geometry->x;
			*popup_sy = *popup_sy + _popup_sy - popup->geometry->y;
			return _popup;
		}

		if ((sx > _popup_sx && sx < _popup_sx + popup_width) &&
				(sy > _popup_sy && sy < _popup_sy + popup_height)) {
			if (pixman_region32_contains_point(&popup->surface->current->input,
						sx - _popup_sx + popup->geometry->x,
						sy - _popup_sy + popup->geometry->y, NULL)) {
				*popup_sx = _popup_sx - popup->geometry->x;
				*popup_sy = _popup_sy - popup->geometry->y;
				return popup;
			}
		}
	}

	return NULL;
}
