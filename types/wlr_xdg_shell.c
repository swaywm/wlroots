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
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>
#include "util/signal.h"
#include "xdg-shell-protocol.h"

static const char *wlr_desktop_xdg_toplevel_role = "xdg_toplevel";
static const char *wlr_desktop_xdg_popup_role = "xdg_popup";

bool wlr_surface_is_xdg_surface(struct wlr_surface *surface) {
	return surface->role != NULL &&
		(strcmp(surface->role, wlr_desktop_xdg_toplevel_role) == 0 ||
		strcmp(surface->role, wlr_desktop_xdg_popup_role) == 0);
}

struct wlr_xdg_surface *wlr_xdg_surface_from_wlr_surface(
		struct wlr_surface *surface) {
	assert(wlr_surface_is_xdg_surface(surface));
	return (struct wlr_xdg_surface *)surface->role_data;
}

struct wlr_xdg_positioner_resource {
	struct wl_resource *resource;
	struct wlr_xdg_positioner attrs;
};

static void resource_handle_destroy(struct wl_client *client,
		struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static void xdg_pointer_grab_end(struct wlr_seat_pointer_grab *grab) {
	struct wlr_xdg_popup_grab *popup_grab = grab->data;

	struct wlr_xdg_popup *popup, *tmp;
	wl_list_for_each_safe(popup, tmp, &popup_grab->popups, grab_link) {
		xdg_popup_send_popup_done(popup->resource);
	}

	wlr_seat_pointer_end_grab(grab->seat);
}

static void xdg_pointer_grab_enter(struct wlr_seat_pointer_grab *grab,
		struct wlr_surface *surface, double sx, double sy) {
	struct wlr_xdg_popup_grab *popup_grab = grab->data;
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
		uint32_t time, enum wlr_axis_orientation orientation, double value,
		int32_t value_discrete, enum wlr_axis_source source) {
	wlr_seat_pointer_send_axis(grab->seat, time, orientation, value,
		value_discrete, source);
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

static void xdg_surface_destroy(struct wlr_xdg_surface *surface);

static void xdg_popup_grab_handle_seat_destroy(
		struct wl_listener *listener, void *data) {
	struct wlr_xdg_popup_grab *xdg_grab =
		wl_container_of(listener, xdg_grab, seat_destroy);

	wl_list_remove(&xdg_grab->seat_destroy.link);

	struct wlr_xdg_popup *popup, *next;
	wl_list_for_each_safe(popup, next, &xdg_grab->popups, grab_link) {
		xdg_surface_destroy(popup->base);
	}

	wl_list_remove(&xdg_grab->link);
	free(xdg_grab);
}

static struct wlr_xdg_popup_grab *xdg_shell_popup_grab_from_seat(
		struct wlr_xdg_shell *shell, struct wlr_seat *seat) {
	struct wlr_xdg_popup_grab *xdg_grab;
	wl_list_for_each(xdg_grab, &shell->popup_grabs, link) {
		if (xdg_grab->seat == seat) {
			return xdg_grab;
		}
	}

	xdg_grab = calloc(1, sizeof(struct wlr_xdg_popup_grab));
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

	xdg_grab->seat_destroy.notify = xdg_popup_grab_handle_seat_destroy;
	wl_signal_add(&seat->events.destroy, &xdg_grab->seat_destroy);

	return xdg_grab;
}


static void xdg_surface_configure_destroy(
		struct wlr_xdg_surface_configure *configure) {
	if (configure == NULL) {
		return;
	}
	wl_list_remove(&configure->link);
	free(configure->toplevel_state);
	free(configure);
}

static void xdg_surface_unmap(struct wlr_xdg_surface *surface) {
	assert(surface->role != WLR_XDG_SURFACE_ROLE_NONE);

	// TODO: probably need to ungrab before this event
	if (surface->mapped) {
		wlr_signal_emit_safe(&surface->events.unmap, surface);
	}

	switch (surface->role) {
	case WLR_XDG_SURFACE_ROLE_TOPLEVEL:
		free(surface->toplevel->title);
		surface->toplevel->title = NULL;
		free(surface->toplevel->app_id);
		surface->toplevel->app_id = NULL;
		break;
	case WLR_XDG_SURFACE_ROLE_POPUP:
		if (surface->popup->seat != NULL) {
			struct wlr_xdg_popup_grab *grab =
				xdg_shell_popup_grab_from_seat(surface->client->shell,
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

static void xdg_toplevel_destroy(struct wlr_xdg_surface *surface) {
	assert(surface->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL);
	xdg_surface_unmap(surface);

	wl_resource_set_user_data(surface->toplevel->resource, NULL);
	free(surface->toplevel);
	surface->toplevel = NULL;

	surface->role = WLR_XDG_SURFACE_ROLE_NONE;
}

static void xdg_popup_destroy(struct wlr_xdg_surface *surface) {
	assert(surface->role == WLR_XDG_SURFACE_ROLE_POPUP);
	xdg_surface_unmap(surface);

	wl_resource_set_user_data(surface->popup->resource, NULL);
	wl_list_remove(&surface->popup->link);
	free(surface->popup);
	surface->popup = NULL;

	surface->role = WLR_XDG_SURFACE_ROLE_NONE;
}

static void xdg_surface_destroy(struct wlr_xdg_surface *surface) {
	if (surface->role != WLR_XDG_SURFACE_ROLE_NONE) {
		xdg_surface_unmap(surface);
	}

	wlr_signal_emit_safe(&surface->events.destroy, surface);

	switch (surface->role) {
	case WLR_XDG_SURFACE_ROLE_TOPLEVEL:
		xdg_toplevel_destroy(surface);
		break;
	case WLR_XDG_SURFACE_ROLE_POPUP:
		xdg_popup_destroy(surface);
		break;
	case WLR_XDG_SURFACE_ROLE_NONE:
		// This space is intentionally left blank
		break;
	}

	wl_resource_set_user_data(surface->resource, NULL);
	wl_list_remove(&surface->link);
	wl_list_remove(&surface->surface_destroy_listener.link);
	wlr_surface_set_role_committed(surface->surface, NULL, NULL);
	free(surface);
}


static const struct xdg_positioner_interface xdg_positioner_implementation;

static struct wlr_xdg_positioner_resource *xdg_positioner_from_resource(
		struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource, &xdg_positioner_interface,
		&xdg_positioner_implementation));
	return wl_resource_get_user_data(resource);
}

static void xdg_positioner_destroy(struct wl_resource *resource) {
	struct wlr_xdg_positioner_resource *positioner =
		xdg_positioner_from_resource(resource);
	free(positioner);
}

static void xdg_positioner_handle_set_size(struct wl_client *client,
		struct wl_resource *resource, int32_t width, int32_t height) {
	struct wlr_xdg_positioner_resource *positioner =
		xdg_positioner_from_resource(resource);

	if (width < 1 || height < 1) {
		wl_resource_post_error(resource,
			XDG_POSITIONER_ERROR_INVALID_INPUT,
			"width and height must be positive and non-zero");
		return;
	}

	positioner->attrs.size.width = width;
	positioner->attrs.size.height = height;
}

static void xdg_positioner_handle_set_anchor_rect(struct wl_client *client,
		struct wl_resource *resource, int32_t x, int32_t y, int32_t width,
		int32_t height) {
	struct wlr_xdg_positioner_resource *positioner =
		xdg_positioner_from_resource(resource);

	if (width < 0 || height < 0) {
		wl_resource_post_error(resource,
			XDG_POSITIONER_ERROR_INVALID_INPUT,
			"width and height must be positive");
		return;
	}

	positioner->attrs.anchor_rect.x = x;
	positioner->attrs.anchor_rect.y = y;
	positioner->attrs.anchor_rect.width = width;
	positioner->attrs.anchor_rect.height = height;
}

static void xdg_positioner_handle_set_anchor(struct wl_client *client,
		struct wl_resource *resource, uint32_t anchor) {
	struct wlr_xdg_positioner_resource *positioner =
		xdg_positioner_from_resource(resource);

	if (anchor > XDG_POSITIONER_ANCHOR_BOTTOM_RIGHT) {
		wl_resource_post_error(resource,
			XDG_POSITIONER_ERROR_INVALID_INPUT,
			"invalid anchor value");
		return;
	}

	positioner->attrs.anchor = anchor;
}

static void xdg_positioner_handle_set_gravity(struct wl_client *client,
		struct wl_resource *resource, uint32_t gravity) {
	struct wlr_xdg_positioner_resource *positioner =
		xdg_positioner_from_resource(resource);

	if (gravity > XDG_POSITIONER_GRAVITY_BOTTOM_RIGHT) {
		wl_resource_post_error(resource,
			XDG_POSITIONER_ERROR_INVALID_INPUT,
			"invalid gravity value");
		return;
	}

	positioner->attrs.gravity = gravity;
}

static void xdg_positioner_handle_set_constraint_adjustment(
		struct wl_client *client, struct wl_resource *resource,
		uint32_t constraint_adjustment) {
	struct wlr_xdg_positioner_resource *positioner =
		xdg_positioner_from_resource(resource);

	positioner->attrs.constraint_adjustment = constraint_adjustment;
}

static void xdg_positioner_handle_set_offset(struct wl_client *client,
		struct wl_resource *resource, int32_t x, int32_t y) {
	struct wlr_xdg_positioner_resource *positioner =
		xdg_positioner_from_resource(resource);

	positioner->attrs.offset.x = x;
	positioner->attrs.offset.y = y;
}

static const struct xdg_positioner_interface
		xdg_positioner_implementation = {
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
	struct wlr_xdg_positioner_resource *positioner =
		calloc(1, sizeof(struct wlr_xdg_positioner_resource));
	if (positioner == NULL) {
		wl_client_post_no_memory(wl_client);
		return;
	}

	positioner->resource = wl_resource_create(wl_client,
		&xdg_positioner_interface,
		wl_resource_get_version(resource),
		id);
	if (positioner->resource == NULL) {
		free(positioner);
		wl_client_post_no_memory(wl_client);
		return;
	}

	wl_resource_set_implementation(positioner->resource,
		&xdg_positioner_implementation,
		positioner, xdg_positioner_destroy);
}

static bool positioner_anchor_has_edge(enum xdg_positioner_anchor anchor,
		enum xdg_positioner_anchor edge) {
	switch (edge) {
	case XDG_POSITIONER_ANCHOR_TOP:
		return anchor == XDG_POSITIONER_ANCHOR_TOP ||
			anchor == XDG_POSITIONER_ANCHOR_TOP_LEFT ||
			anchor == XDG_POSITIONER_ANCHOR_TOP_RIGHT;
	case XDG_POSITIONER_ANCHOR_BOTTOM:
		return anchor == XDG_POSITIONER_ANCHOR_BOTTOM ||
			anchor == XDG_POSITIONER_ANCHOR_BOTTOM_LEFT ||
			anchor == XDG_POSITIONER_ANCHOR_BOTTOM_RIGHT;
	case XDG_POSITIONER_ANCHOR_LEFT:
		return anchor == XDG_POSITIONER_ANCHOR_LEFT ||
			anchor == XDG_POSITIONER_ANCHOR_TOP_LEFT ||
			anchor == XDG_POSITIONER_ANCHOR_BOTTOM_LEFT;
	case XDG_POSITIONER_ANCHOR_RIGHT:
		return anchor == XDG_POSITIONER_ANCHOR_RIGHT ||
			anchor == XDG_POSITIONER_ANCHOR_TOP_RIGHT ||
			anchor == XDG_POSITIONER_ANCHOR_BOTTOM_RIGHT;
	default:
		assert(false); // not reached
	}
}

static bool positioner_gravity_has_edge(enum xdg_positioner_gravity gravity,
		enum xdg_positioner_gravity edge) {
	// gravity and edge enums are the same
	return positioner_anchor_has_edge((enum xdg_positioner_anchor)gravity,
		(enum xdg_positioner_anchor)edge);
}

struct wlr_box wlr_xdg_positioner_get_geometry(
		struct wlr_xdg_positioner *positioner) {
	struct wlr_box geometry = {
		.x = positioner->offset.x,
		.y = positioner->offset.y,
		.width = positioner->size.width,
		.height = positioner->size.height,
	};

	if (positioner_anchor_has_edge(positioner->anchor,
			XDG_POSITIONER_ANCHOR_TOP)) {
		geometry.y += positioner->anchor_rect.y;
	} else if (positioner_anchor_has_edge(positioner->anchor,
			XDG_POSITIONER_ANCHOR_BOTTOM)) {
		geometry.y +=
			positioner->anchor_rect.y + positioner->anchor_rect.height;
	} else {
		geometry.y +=
			positioner->anchor_rect.y + positioner->anchor_rect.height / 2;
	}

	if (positioner_anchor_has_edge(positioner->anchor,
			XDG_POSITIONER_ANCHOR_LEFT)) {
		geometry.x += positioner->anchor_rect.x;
	} else if (positioner_anchor_has_edge(positioner->anchor,
			XDG_POSITIONER_ANCHOR_RIGHT)) {
		geometry.x += positioner->anchor_rect.x + positioner->anchor_rect.width;
	} else {
		geometry.x +=
			positioner->anchor_rect.x + positioner->anchor_rect.width / 2;
	}

	if (positioner_gravity_has_edge(positioner->gravity,
			XDG_POSITIONER_GRAVITY_TOP)) {
		geometry.y -= geometry.height;
	} else if (positioner_gravity_has_edge(positioner->gravity,
			XDG_POSITIONER_GRAVITY_BOTTOM)) {
		geometry.y = geometry.y;
	} else {
		geometry.y -= geometry.height / 2;
	}

	if (positioner_gravity_has_edge(positioner->gravity,
			XDG_POSITIONER_GRAVITY_LEFT)) {
		geometry.x -= geometry.width;
	} else if (positioner_gravity_has_edge(positioner->gravity,
			XDG_POSITIONER_GRAVITY_RIGHT)) {
		geometry.x = geometry.x;
	} else {
		geometry.x -= geometry.width / 2;
	}

	if (positioner->constraint_adjustment ==
			XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_NONE) {
		return geometry;
	}

	return geometry;
}


static const struct xdg_popup_interface xdg_popup_implementation;

struct wlr_xdg_surface *wlr_xdg_surface_from_popup_resource(
		struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource, &xdg_popup_interface,
		&xdg_popup_implementation));
	return wl_resource_get_user_data(resource);
}

static void xdg_popup_handle_grab(struct wl_client *client,
		struct wl_resource *resource, struct wl_resource *seat_resource,
		uint32_t serial) {
	struct wlr_xdg_surface *surface =
		wlr_xdg_surface_from_popup_resource(resource);
	struct wlr_seat_client *seat_client =
		wlr_seat_client_from_resource(seat_resource);

	if (surface->popup->committed) {
		wl_resource_post_error(surface->popup->resource,
			XDG_POPUP_ERROR_INVALID_GRAB,
			"xdg_popup is already mapped");
		return;
	}

	struct wlr_xdg_popup_grab *popup_grab =
		xdg_shell_popup_grab_from_seat(surface->client->shell,
			seat_client->seat);

	if (!wl_list_empty(&surface->popups)) {
		wl_resource_post_error(surface->client->resource,
			XDG_WM_BASE_ERROR_NOT_THE_TOPMOST_POPUP,
			"xdg_popup was not created on the topmost popup");
		return;
	}

	popup_grab->client = surface->client->client;
	surface->popup->seat = seat_client->seat;

	wl_list_insert(&popup_grab->popups, &surface->popup->grab_link);

	wlr_seat_pointer_start_grab(seat_client->seat,
		&popup_grab->pointer_grab);
	wlr_seat_keyboard_start_grab(seat_client->seat,
		&popup_grab->keyboard_grab);
}

static void xdg_popup_handle_destroy(struct wl_client *client,
		struct wl_resource *resource) {
	struct wlr_xdg_surface *surface =
		wlr_xdg_surface_from_popup_resource(resource);

	if (!wl_list_empty(&surface->popups)) {
		wl_resource_post_error(surface->client->resource,
			XDG_WM_BASE_ERROR_NOT_THE_TOPMOST_POPUP,
			"xdg_popup was destroyed while it was not the topmost popup");
		return;
	}

	wl_resource_destroy(resource);
}

static const struct xdg_popup_interface xdg_popup_implementation = {
	.destroy = xdg_popup_handle_destroy,
	.grab = xdg_popup_handle_grab,
};

static void xdg_popup_resource_destroy(struct wl_resource *resource) {
	struct wlr_xdg_surface *surface =
		wlr_xdg_surface_from_popup_resource(resource);
	if (surface != NULL) {
		xdg_popup_destroy(surface);
	}
}

static const struct xdg_surface_interface xdg_surface_implementation;

struct wlr_xdg_surface *wlr_xdg_surface_from_resource(
		struct wl_resource *resource) {
	// TODO: Double check that all of the callers can deal with NULL
	if (!resource) {
		return NULL;
	}
	assert(wl_resource_instance_of(resource, &xdg_surface_interface,
		&xdg_surface_implementation));
	return wl_resource_get_user_data(resource);
}

static void xdg_surface_handle_get_popup(struct wl_client *client,
		struct wl_resource *resource, uint32_t id,
		struct wl_resource *parent_resource,
		struct wl_resource *positioner_resource) {
	struct wlr_xdg_surface *surface =
		wlr_xdg_surface_from_resource(resource);
	struct wlr_xdg_surface *parent =
		wlr_xdg_surface_from_resource(parent_resource);
	struct wlr_xdg_positioner_resource *positioner =
		xdg_positioner_from_resource(positioner_resource);

	if (positioner->attrs.size.width == 0 ||
			positioner->attrs.anchor_rect.width == 0) {
		wl_resource_post_error(resource,
			XDG_WM_BASE_ERROR_INVALID_POSITIONER,
			"positioner object is not complete");
		return;
	}

	if (wlr_surface_set_role(surface->surface, wlr_desktop_xdg_popup_role,
			resource, XDG_WM_BASE_ERROR_ROLE)) {
		return;
	}

	surface->popup = calloc(1, sizeof(struct wlr_xdg_popup));
	if (!surface->popup) {
		wl_resource_post_no_memory(resource);
		return;
	}

	surface->popup->resource =
		wl_resource_create(client, &xdg_popup_interface,
			wl_resource_get_version(resource), id);
	if (surface->popup->resource == NULL) {
		free(surface->popup);
		wl_resource_post_no_memory(resource);
		return;
	}

	surface->role = WLR_XDG_SURFACE_ROLE_POPUP;
	surface->popup->base = surface;

	// positioner properties
	memcpy(&surface->popup->positioner, &positioner->attrs,
		sizeof(struct wlr_xdg_positioner));
	surface->popup->geometry =
		wlr_xdg_positioner_get_geometry(&positioner->attrs);

	wl_resource_set_implementation(surface->popup->resource,
		&xdg_popup_implementation, surface,
		xdg_popup_resource_destroy);

	if (parent) {
		surface->popup->parent = parent->surface;
		wl_list_insert(&parent->popups, &surface->popup->link);
		wlr_signal_emit_safe(&parent->events.new_popup, surface->popup);
	}
}


static const struct xdg_toplevel_interface xdg_toplevel_implementation;

static struct wlr_xdg_surface *xdg_surface_from_xdg_toplevel_resource(
		struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource, &xdg_toplevel_interface,
		&xdg_toplevel_implementation));
	return wl_resource_get_user_data(resource);
}

static void xdg_toplevel_handle_set_parent(struct wl_client *client,
		struct wl_resource *resource, struct wl_resource *parent_resource) {
	struct wlr_xdg_surface *surface =
		xdg_surface_from_xdg_toplevel_resource(resource);
	struct wlr_xdg_surface *parent = NULL;

	if (parent_resource != NULL) {
		parent = xdg_surface_from_xdg_toplevel_resource(parent_resource);
	}

	surface->toplevel->parent = parent;
}

static void xdg_toplevel_handle_set_title(struct wl_client *client,
		struct wl_resource *resource, const char *title) {
	struct wlr_xdg_surface *surface =
		xdg_surface_from_xdg_toplevel_resource(resource);
	char *tmp;

	tmp = strdup(title);
	if (tmp == NULL) {
		return;
	}

	free(surface->toplevel->title);
	surface->toplevel->title = tmp;
}

static void xdg_toplevel_handle_set_app_id(struct wl_client *client,
		struct wl_resource *resource, const char *app_id) {
	struct wlr_xdg_surface *surface =
		xdg_surface_from_xdg_toplevel_resource(resource);
	char *tmp;

	tmp = strdup(app_id);
	if (tmp == NULL) {
		return;
	}

	free(surface->toplevel->app_id);
	surface->toplevel->app_id = tmp;
}

static void xdg_toplevel_handle_show_window_menu(struct wl_client *client,
		struct wl_resource *resource, struct wl_resource *seat_resource,
		uint32_t serial, int32_t x, int32_t y) {
	struct wlr_xdg_surface *surface =
		xdg_surface_from_xdg_toplevel_resource(resource);
	struct wlr_seat_client *seat =
		wlr_seat_client_from_resource(seat_resource);

	if (!surface->configured) {
		wl_resource_post_error(surface->toplevel->resource,
			XDG_SURFACE_ERROR_NOT_CONSTRUCTED,
			"surface has not been configured yet");
		return;
	}

	if (!wlr_seat_validate_grab_serial(seat->seat, serial)) {
		wlr_log(L_DEBUG, "invalid serial for grab");
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
		xdg_surface_from_xdg_toplevel_resource(resource);
	struct wlr_seat_client *seat =
		wlr_seat_client_from_resource(seat_resource);

	if (!surface->configured) {
		wl_resource_post_error(surface->toplevel->resource,
			XDG_SURFACE_ERROR_NOT_CONSTRUCTED,
			"surface has not been configured yet");
		return;
	}

	if (!wlr_seat_validate_grab_serial(seat->seat, serial)) {
		wlr_log(L_DEBUG, "invalid serial for grab");
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
		xdg_surface_from_xdg_toplevel_resource(resource);
	struct wlr_seat_client *seat =
		wlr_seat_client_from_resource(seat_resource);

	if (!surface->configured) {
		wl_resource_post_error(surface->toplevel->resource,
			XDG_SURFACE_ERROR_NOT_CONSTRUCTED,
			"surface has not been configured yet");
		return;
	}

	if (!wlr_seat_validate_grab_serial(seat->seat, serial)) {
		wlr_log(L_DEBUG, "invalid serial for grab");
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
		xdg_surface_from_xdg_toplevel_resource(resource);
	surface->toplevel->client_pending.max_width = width;
	surface->toplevel->client_pending.max_height = height;
}

static void xdg_toplevel_handle_set_min_size(struct wl_client *client,
		struct wl_resource *resource, int32_t width, int32_t height) {
	struct wlr_xdg_surface *surface =
		xdg_surface_from_xdg_toplevel_resource(resource);
	surface->toplevel->client_pending.min_width = width;
	surface->toplevel->client_pending.min_height = height;
}

static void xdg_toplevel_handle_set_maximized(struct wl_client *client,
		struct wl_resource *resource) {
	struct wlr_xdg_surface *surface =
		xdg_surface_from_xdg_toplevel_resource(resource);
	surface->toplevel->client_pending.maximized = true;
	wlr_signal_emit_safe(&surface->toplevel->events.request_maximize, surface);
}

static void xdg_toplevel_handle_unset_maximized(struct wl_client *client,
		struct wl_resource *resource) {
	struct wlr_xdg_surface *surface =
		xdg_surface_from_xdg_toplevel_resource(resource);
	surface->toplevel->client_pending.maximized = false;
	wlr_signal_emit_safe(&surface->toplevel->events.request_maximize, surface);
}

static void xdg_toplevel_handle_set_fullscreen(struct wl_client *client,
		struct wl_resource *resource, struct wl_resource *output_resource) {
	struct wlr_xdg_surface *surface =
		xdg_surface_from_xdg_toplevel_resource(resource);

	struct wlr_output *output = NULL;
	if (output_resource != NULL) {
		output = wlr_output_from_resource(output_resource);
	}

	surface->toplevel->client_pending.fullscreen = true;

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
		xdg_surface_from_xdg_toplevel_resource(resource);

	surface->toplevel->client_pending.fullscreen = false;

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
		xdg_surface_from_xdg_toplevel_resource(resource);
	wlr_signal_emit_safe(&surface->toplevel->events.request_minimize, surface);
}

static const struct xdg_toplevel_interface xdg_toplevel_implementation = {
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
	.set_minimized = xdg_toplevel_handle_set_minimized,
};

static void xdg_surface_resource_destroy(struct wl_resource *resource) {
	struct wlr_xdg_surface *surface =
		wlr_xdg_surface_from_resource(resource);
	if (surface != NULL) {
		xdg_surface_destroy(surface);
	}
}

static void xdg_toplevel_resource_destroy(struct wl_resource *resource) {
	struct wlr_xdg_surface *surface =
		xdg_surface_from_xdg_toplevel_resource(resource);
	if (surface != NULL) {
		xdg_toplevel_destroy(surface);
	}
}

static void xdg_surface_handle_get_toplevel(struct wl_client *client,
		struct wl_resource *resource, uint32_t id) {
	struct wlr_xdg_surface *surface = wlr_xdg_surface_from_resource(resource);

	if (wlr_surface_set_role(surface->surface, wlr_desktop_xdg_toplevel_role,
			resource, XDG_WM_BASE_ERROR_ROLE)) {
		return;
	}

	surface->toplevel = calloc(1, sizeof(struct wlr_xdg_toplevel));
	if (surface->toplevel == NULL) {
		wl_resource_post_no_memory(resource);
		return;
	}
	wl_signal_init(&surface->toplevel->events.request_maximize);
	wl_signal_init(&surface->toplevel->events.request_fullscreen);
	wl_signal_init(&surface->toplevel->events.request_minimize);
	wl_signal_init(&surface->toplevel->events.request_move);
	wl_signal_init(&surface->toplevel->events.request_resize);
	wl_signal_init(&surface->toplevel->events.request_show_window_menu);

	surface->role = WLR_XDG_SURFACE_ROLE_TOPLEVEL;
	surface->toplevel->base = surface;

	struct wl_resource *toplevel_resource = wl_resource_create(client,
		&xdg_toplevel_interface, wl_resource_get_version(resource), id);
	if (toplevel_resource == NULL) {
		free(surface->toplevel);
		wl_resource_post_no_memory(resource);
		return;
	}

	surface->toplevel->resource = toplevel_resource;

	wl_resource_set_implementation(toplevel_resource,
		&xdg_toplevel_implementation, surface,
		xdg_toplevel_resource_destroy);
}

static void xdg_toplevel_ack_configure(
		struct wlr_xdg_surface *surface,
		struct wlr_xdg_surface_configure *configure) {
	assert(surface->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL);
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

static void xdg_surface_handle_ack_configure(struct wl_client *client,
		struct wl_resource *resource, uint32_t serial) {
	struct wlr_xdg_surface *surface = wlr_xdg_surface_from_resource(resource);

	if (surface->role == WLR_XDG_SURFACE_ROLE_NONE) {
		wl_resource_post_error(surface->resource,
			XDG_SURFACE_ERROR_NOT_CONSTRUCTED,
			"xdg_surface must have a role");
		return;
	}

	bool found = false;
	struct wlr_xdg_surface_configure *configure, *tmp;
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
			XDG_WM_BASE_ERROR_INVALID_SURFACE_STATE,
			"wrong configure serial: %u", serial);
		return;
	}

	switch (surface->role) {
	case WLR_XDG_SURFACE_ROLE_NONE:
		assert(0 && "not reached");
		break;
	case WLR_XDG_SURFACE_ROLE_TOPLEVEL:
		xdg_toplevel_ack_configure(surface, configure);
		break;
	case WLR_XDG_SURFACE_ROLE_POPUP:
		break;
	}

	surface->configured = true;
	surface->configure_serial = serial;

	xdg_surface_configure_destroy(configure);
}

static void xdg_surface_handle_set_window_geometry(struct wl_client *client,
		struct wl_resource *resource, int32_t x, int32_t y, int32_t width,
		int32_t height) {
	struct wlr_xdg_surface *surface = wlr_xdg_surface_from_resource(resource);

	if (surface->role == WLR_XDG_SURFACE_ROLE_NONE) {
		wl_resource_post_error(surface->resource,
			XDG_SURFACE_ERROR_NOT_CONSTRUCTED,
			"xdg_surface must have a role");
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

	if (surface->role != WLR_XDG_SURFACE_ROLE_NONE) {
		wlr_log(L_ERROR, "Tried to destroy an xdg_surface before its role "
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

static bool xdg_surface_toplevel_state_compare(
		struct wlr_xdg_toplevel *state) {
	struct {
		struct wlr_xdg_toplevel_state state;
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
		struct wlr_xdg_surface_configure *configure =
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

static void toplevel_send_configure(
		struct wlr_xdg_surface *surface,
		struct wlr_xdg_surface_configure *configure) {
	assert(surface->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL);

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
			wlr_log(L_ERROR, "Could not allocate state for maximized xdg_toplevel");
			goto error_out;
		}
		*s = XDG_TOPLEVEL_STATE_MAXIMIZED;
	}
	if (surface->toplevel->server_pending.fullscreen) {
		s = wl_array_add(&states, sizeof(uint32_t));
		if (!s) {
			wlr_log(L_ERROR, "Could not allocate state for fullscreen xdg_toplevel");
			goto error_out;
		}
		*s = XDG_TOPLEVEL_STATE_FULLSCREEN;
	}
	if (surface->toplevel->server_pending.resizing) {
		s = wl_array_add(&states, sizeof(uint32_t));
		if (!s) {
			wlr_log(L_ERROR, "Could not allocate state for resizing xdg_toplevel");
			goto error_out;
		}
		*s = XDG_TOPLEVEL_STATE_RESIZING;
	}
	if (surface->toplevel->server_pending.activated) {
		s = wl_array_add(&states, sizeof(uint32_t));
		if (!s) {
			wlr_log(L_ERROR, "Could not allocate state for activated xdg_toplevel");
			goto error_out;
		}
		*s = XDG_TOPLEVEL_STATE_ACTIVATED;
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

	switch (surface->role) {
	case WLR_XDG_SURFACE_ROLE_NONE:
		assert(0 && "not reached");
		break;
	case WLR_XDG_SURFACE_ROLE_TOPLEVEL:
		toplevel_send_configure(surface, configure);
		break;
	case WLR_XDG_SURFACE_ROLE_POPUP:
		xdg_popup_send_configure(surface->popup->resource,
			surface->popup->geometry.x,
			surface->popup->geometry.y,
			surface->popup->geometry.width,
			surface->popup->geometry.height);
		break;
	}

	xdg_surface_send_configure(surface->resource, configure->serial);
}

static uint32_t xdg_surface_schedule_configure(
		struct wlr_xdg_surface *surface) {
	struct wl_display *display = wl_client_get_display(surface->client->client);
	struct wl_event_loop *loop = wl_display_get_event_loop(display);
	bool pending_same = false;

	switch (surface->role) {
	case WLR_XDG_SURFACE_ROLE_NONE:
		assert(0 && "not reached");
		break;
	case WLR_XDG_SURFACE_ROLE_TOPLEVEL:
		pending_same =
			xdg_surface_toplevel_state_compare(surface->toplevel);
		break;
	case WLR_XDG_SURFACE_ROLE_POPUP:
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
			surface_send_configure, surface);
		return surface->configure_next_serial;
	}
}

static void xdg_surface_handle_surface_destroy(struct wl_listener *listener,
		void *data) {
	struct wlr_xdg_surface *xdg_surface =
		wl_container_of(listener, xdg_surface, surface_destroy_listener);
	xdg_surface_destroy(xdg_surface);
}

static void xdg_surface_toplevel_committed(
		struct wlr_xdg_surface *surface) {
	assert(surface->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL);

	if (!surface->toplevel->added) {
		// on the first commit, send a configure request to tell the client it
		// is added
		xdg_surface_schedule_configure(surface);
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

static void xdg_surface_popup_committed(
		struct wlr_xdg_surface *surface) {
	assert(surface->role == WLR_XDG_SURFACE_ROLE_POPUP);

	if (!surface->popup->parent) {
		wl_resource_post_error(surface->resource,
			XDG_SURFACE_ERROR_NOT_CONSTRUCTED,
			"xdg_popup has no parent");
		return;
	}

	if (!surface->popup->committed) {
		xdg_surface_schedule_configure(surface);
		surface->popup->committed = true;
	}
}

static void handle_surface_committed(struct wlr_surface *wlr_surface,
		void *role_data) {
	struct wlr_xdg_surface *surface = role_data;

	if (wlr_surface_has_buffer(surface->surface) && !surface->configured) {
		wl_resource_post_error(surface->resource,
			XDG_SURFACE_ERROR_UNCONFIGURED_BUFFER,
			"xdg_surface has never been configured");
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
		wl_resource_post_error(surface->resource,
			XDG_SURFACE_ERROR_NOT_CONSTRUCTED,
			"xdg_surface must have a role");
		break;
	case WLR_XDG_SURFACE_ROLE_TOPLEVEL:
		xdg_surface_toplevel_committed(surface);
		break;
	case WLR_XDG_SURFACE_ROLE_POPUP:
		xdg_surface_popup_committed(surface);
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

static const struct xdg_wm_base_interface xdg_shell_impl;

static struct wlr_xdg_client *xdg_client_from_resource(
		struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource, &xdg_wm_base_interface,
		&xdg_shell_impl));
	return wl_resource_get_user_data(resource);
}

static void xdg_shell_handle_get_xdg_surface(struct wl_client *wl_client,
		struct wl_resource *client_resource, uint32_t id,
		struct wl_resource *surface_resource) {
	struct wlr_xdg_client *client =
		xdg_client_from_resource(client_resource);

	struct wlr_xdg_surface *surface =
		calloc(1, sizeof(struct wlr_xdg_surface));
	if (surface == NULL) {
		wl_client_post_no_memory(wl_client);
		return;
	}

	surface->client = client;
	surface->role = WLR_XDG_SURFACE_ROLE_NONE;
	surface->surface = wlr_surface_from_resource(surface_resource);
	surface->resource = wl_resource_create(wl_client,
		&xdg_surface_interface, wl_resource_get_version(client_resource),
		id);
	if (surface->resource == NULL) {
		free(surface);
		wl_client_post_no_memory(wl_client);
		return;
	}

	if (wlr_surface_has_buffer(surface->surface)) {
		wl_resource_destroy(surface->resource);
		free(surface);
		wl_resource_post_error(surface_resource,
			XDG_SURFACE_ERROR_UNCONFIGURED_BUFFER,
			"xdg_surface must not have a buffer at creation");
		return;
	}

	wl_list_init(&surface->configure_list);
	wl_list_init(&surface->popups);

	wl_signal_init(&surface->events.destroy);
	wl_signal_init(&surface->events.ping_timeout);
	wl_signal_init(&surface->events.new_popup);
	wl_signal_init(&surface->events.map);
	wl_signal_init(&surface->events.unmap);

	wl_signal_add(&surface->surface->events.destroy,
		&surface->surface_destroy_listener);
	surface->surface_destroy_listener.notify =
		xdg_surface_handle_surface_destroy;

	wlr_surface_set_role_committed(surface->surface,
		handle_surface_committed, surface);

	wlr_log(L_DEBUG, "new xdg_surface %p (res %p)", surface, surface->resource);
	wl_resource_set_implementation(surface->resource,
		&xdg_surface_implementation, surface, xdg_surface_resource_destroy);
	wl_list_insert(&client->surfaces, &surface->link);
}

static void xdg_shell_handle_pong(struct wl_client *wl_client,
		struct wl_resource *resource, uint32_t serial) {
	struct wlr_xdg_client *client = xdg_client_from_resource(resource);

	if (client->ping_serial != serial) {
		return;
	}

	wl_event_source_timer_update(client->ping_timer, 0);
	client->ping_serial = 0;
}

static void xdg_shell_handle_destroy(struct wl_client *wl_client,
		struct wl_resource *resource) {
	struct wlr_xdg_client *client = xdg_client_from_resource(resource);

	if (!wl_list_empty(&client->surfaces)) {
		wl_resource_post_error(client->resource,
			XDG_WM_BASE_ERROR_DEFUNCT_SURFACES,
			"xdg_wm_base was destroyed before children");
		return;
	}

	wl_resource_destroy(resource);
}

static const struct xdg_wm_base_interface xdg_shell_impl = {
	.destroy = xdg_shell_handle_destroy,
	.create_positioner = xdg_shell_handle_create_positioner,
	.get_xdg_surface = xdg_shell_handle_get_xdg_surface,
	.pong = xdg_shell_handle_pong,
};

static void xdg_client_handle_resource_destroy(struct wl_resource *resource) {
	struct wlr_xdg_client *client = xdg_client_from_resource(resource);

	struct wlr_xdg_surface *surface, *tmp = NULL;
	wl_list_for_each_safe(surface, tmp, &client->surfaces, link) {
		xdg_surface_destroy(surface);
	}

	if (client->ping_timer != NULL) {
		wl_event_source_remove(client->ping_timer);
	}

	wl_list_remove(&client->link);
	free(client);
}

static int xdg_client_ping_timeout(void *user_data) {
	struct wlr_xdg_client *client = user_data;

	struct wlr_xdg_surface *surface;
	wl_list_for_each(surface, &client->surfaces, link) {
		wlr_signal_emit_safe(&surface->events.ping_timeout, surface);
	}

	client->ping_serial = 0;
	return 1;
}

static void xdg_shell_bind(struct wl_client *wl_client, void *data,
		uint32_t version, uint32_t id) {
	struct wlr_xdg_shell *xdg_shell = data;
	assert(wl_client && xdg_shell);

	struct wlr_xdg_client *client =
		calloc(1, sizeof(struct wlr_xdg_client));
	if (client == NULL) {
		wl_client_post_no_memory(wl_client);
		return;
	}

	wl_list_init(&client->surfaces);

	client->resource =
		wl_resource_create(wl_client, &xdg_wm_base_interface, version, id);
	if (client->resource == NULL) {
		free(client);
		wl_client_post_no_memory(wl_client);
		return;
	}
	client->client = wl_client;
	client->shell = xdg_shell;

	wl_resource_set_implementation(client->resource, &xdg_shell_impl, client,
		xdg_client_handle_resource_destroy);
	wl_list_insert(&xdg_shell->clients, &client->link);

	struct wl_display *display = wl_client_get_display(client->client);
	struct wl_event_loop *loop = wl_display_get_event_loop(display);
	client->ping_timer = wl_event_loop_add_timer(loop,
		xdg_client_ping_timeout, client);
	if (client->ping_timer == NULL) {
		wl_client_post_no_memory(client->client);
	}
}

static void handle_display_destroy(struct wl_listener *listener, void *data) {
	struct wlr_xdg_shell *xdg_shell =
		wl_container_of(listener, xdg_shell, display_destroy);
	wlr_xdg_shell_destroy(xdg_shell);
}

struct wlr_xdg_shell *wlr_xdg_shell_create(struct wl_display *display) {
	struct wlr_xdg_shell *xdg_shell =
		calloc(1, sizeof(struct wlr_xdg_shell));
	if (!xdg_shell) {
		return NULL;
	}

	xdg_shell->ping_timeout = 10000;

	wl_list_init(&xdg_shell->clients);
	wl_list_init(&xdg_shell->popup_grabs);

	struct wl_global *wl_global = wl_global_create(display,
		&xdg_wm_base_interface, 1, xdg_shell, xdg_shell_bind);
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

void wlr_xdg_shell_destroy(struct wlr_xdg_shell *xdg_shell) {
	if (!xdg_shell) {
		return;
	}
	wl_list_remove(&xdg_shell->display_destroy.link);
	wl_global_destroy(xdg_shell->wl_global);
	free(xdg_shell);
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

uint32_t wlr_xdg_toplevel_set_size(struct wlr_xdg_surface *surface,
		uint32_t width, uint32_t height) {
	assert(surface->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL);
	surface->toplevel->server_pending.width = width;
	surface->toplevel->server_pending.height = height;

	return xdg_surface_schedule_configure(surface);
}

uint32_t wlr_xdg_toplevel_set_activated(struct wlr_xdg_surface *surface,
		bool activated) {
	assert(surface->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL);
	surface->toplevel->server_pending.activated = activated;

	return xdg_surface_schedule_configure(surface);
}

uint32_t wlr_xdg_toplevel_set_maximized(struct wlr_xdg_surface *surface,
		bool maximized) {
	assert(surface->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL);
	surface->toplevel->server_pending.maximized = maximized;

	return xdg_surface_schedule_configure(surface);
}

uint32_t wlr_xdg_toplevel_set_fullscreen(struct wlr_xdg_surface *surface,
		bool fullscreen) {
	assert(surface->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL);
	surface->toplevel->server_pending.fullscreen = fullscreen;

	return xdg_surface_schedule_configure(surface);
}

uint32_t wlr_xdg_toplevel_set_resizing(struct wlr_xdg_surface *surface,
		bool resizing) {
	assert(surface->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL);
	surface->toplevel->server_pending.resizing = resizing;

	return xdg_surface_schedule_configure(surface);
}

void wlr_xdg_surface_send_close(struct wlr_xdg_surface *surface) {
	switch (surface->role) {
	case WLR_XDG_SURFACE_ROLE_NONE:
		assert(0 && "not reached");
		break;
	case WLR_XDG_SURFACE_ROLE_TOPLEVEL:
		if (surface->toplevel) {
			xdg_toplevel_send_close(surface->toplevel->resource);
		}
		break;
	case WLR_XDG_SURFACE_ROLE_POPUP:
		if (surface->popup) {
			xdg_popup_send_popup_done(surface->popup->resource);
		}
		break;
	}
}

void wlr_xdg_surface_popup_get_position(struct wlr_xdg_surface *surface,
		double *popup_sx, double *popup_sy) {
	assert(surface->role == WLR_XDG_SURFACE_ROLE_POPUP);
	struct wlr_xdg_popup *popup = surface->popup;
	assert(strcmp(popup->parent->role, wlr_desktop_xdg_toplevel_role) == 0
			|| strcmp(popup->parent->role, wlr_desktop_xdg_popup_role) == 0);
	struct wlr_xdg_surface *parent = popup->parent->role_data;
	*popup_sx = parent->geometry.x + popup->geometry.x - surface->geometry.x;
	*popup_sy = parent->geometry.y + popup->geometry.y - surface->geometry.y;
}

struct wlr_surface *wlr_xdg_surface_surface_at(
		struct wlr_xdg_surface *surface, double sx, double sy,
		double *sub_x, double *sub_y) {
	struct wlr_xdg_popup *popup_state;
	wl_list_for_each(popup_state, &surface->popups, link) {
		struct wlr_xdg_surface *popup = popup_state->base;

		double popup_sx, popup_sy;
		wlr_xdg_surface_popup_get_position(popup, &popup_sx, &popup_sy);

		struct wlr_surface *sub = wlr_xdg_surface_surface_at(popup,
			sx - popup_sx,
			sy - popup_sy,
			sub_x, sub_y);
		if (sub != NULL) {
			return sub;
		}
	}

	return wlr_surface_surface_at(surface->surface, sx, sy, sub_x, sub_y);
}

void wlr_xdg_popup_get_anchor_point(struct wlr_xdg_popup *popup,
		int *root_sx, int *root_sy) {
	struct wlr_box rect = popup->positioner.anchor_rect;
	enum xdg_positioner_anchor anchor = popup->positioner.anchor;
	int sx = 0, sy = 0;

	if (anchor == XDG_POSITIONER_ANCHOR_NONE) {
		sx = (rect.x + rect.width) / 2;
		sy = (rect.y + rect.height) / 2;
	} else if (anchor == XDG_POSITIONER_ANCHOR_TOP) {
		sx = (rect.x + rect.width) / 2;
		sy = rect.y;
	} else if (anchor == XDG_POSITIONER_ANCHOR_BOTTOM) {
		sx = (rect.x + rect.width) / 2;
		sy = rect.y + rect.height;
	} else if (anchor == XDG_POSITIONER_ANCHOR_LEFT) {
		sx = rect.x;
		sy = (rect.y + rect.height) / 2;
	} else if (anchor == XDG_POSITIONER_ANCHOR_RIGHT) {
		sx = rect.x + rect.width;
		sy = (rect.y + rect.height) / 2;
	} else if (anchor == XDG_POSITIONER_ANCHOR_TOP_LEFT) {
		sx = rect.x;
		sy = rect.y;
	} else if (anchor == XDG_POSITIONER_ANCHOR_TOP_RIGHT) {
		sx = rect.x + rect.width;
		sy = rect.y;
	} else if (anchor == XDG_POSITIONER_ANCHOR_BOTTOM_LEFT) {
		sx = rect.x;
		sy = rect.y + rect.height;
	} else if (anchor == XDG_POSITIONER_ANCHOR_BOTTOM_RIGHT) {
		sx = rect.x + rect.width;
		sy = rect.y + rect.height;
	}

	*root_sx = sx;
	*root_sy = sy;
}

void wlr_xdg_popup_get_toplevel_coords(struct wlr_xdg_popup *popup,
		int popup_sx, int popup_sy, int *toplevel_sx, int *toplevel_sy) {
	struct wlr_xdg_surface *parent =
		wlr_xdg_surface_from_wlr_surface(popup->parent);
	while (parent != NULL && parent->role == WLR_XDG_SURFACE_ROLE_POPUP) {
		popup_sx += parent->popup->geometry.x;
		popup_sy += parent->popup->geometry.y;
		parent = wlr_xdg_surface_from_wlr_surface(parent->popup->parent);
	}
	assert(parent);

	*toplevel_sx = popup_sx + parent->geometry.x;
	*toplevel_sy = popup_sy + parent->geometry.y;
}

static void xdg_popup_box_constraints(struct wlr_xdg_popup *popup,
		struct wlr_box *toplevel_sx_box, int *offset_x, int *offset_y) {
	int popup_width = popup->geometry.width;
	int popup_height = popup->geometry.height;
	int anchor_sx = 0, anchor_sy = 0;
	wlr_xdg_popup_get_anchor_point(popup, &anchor_sx, &anchor_sy);
	int popup_sx = 0, popup_sy = 0;
	wlr_xdg_popup_get_toplevel_coords(popup, popup->geometry.x,
		popup->geometry.y, &popup_sx, &popup_sy);
	*offset_x = 0, *offset_y = 0;

	if (popup_sx < toplevel_sx_box->x) {
		*offset_x = toplevel_sx_box->x - popup_sx;
	} else if (popup_sx + popup_width >
			toplevel_sx_box->x + toplevel_sx_box->width) {
		*offset_x = toplevel_sx_box->x + toplevel_sx_box->width -
			(popup_sx + popup_width);
	}

	if (popup_sy < toplevel_sx_box->y) {
		*offset_y = toplevel_sx_box->y - popup_sy;
	} else if (popup_sy + popup_height >
			toplevel_sx_box->y + toplevel_sx_box->height) {
		*offset_y = toplevel_sx_box->y + toplevel_sx_box->height -
			(popup_sy + popup_height);
	}
}

static bool xdg_popup_unconstrain_flip(struct wlr_xdg_popup *popup,
		struct wlr_box *toplevel_sx_box) {
	int offset_x = 0, offset_y = 0;
	xdg_popup_box_constraints(popup, toplevel_sx_box,
		&offset_x, &offset_y);

	if (!offset_x && !offset_y) {
		return true;
	}

	bool flip_x = offset_x &&
		(popup->positioner.constraint_adjustment &
		 XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_FLIP_X);

	bool flip_y = offset_y &&
		(popup->positioner.constraint_adjustment &
		 XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_FLIP_Y);

	if (flip_x) {
		wlr_positioner_invert_x(&popup->positioner);
	}
	if (flip_y) {
		wlr_positioner_invert_y(&popup->positioner);
	}

	popup->geometry =
		wlr_xdg_positioner_get_geometry(&popup->positioner);

	xdg_popup_box_constraints(popup, toplevel_sx_box,
		&offset_x, &offset_y);

	if (!offset_x && !offset_y) {
		// no longer constrained
		return true;
	}

	// revert the positioner back if it didn't fix it and go to the next part
	if (flip_x) {
		wlr_positioner_invert_x(&popup->positioner);
	}
	if (flip_y) {
		wlr_positioner_invert_y(&popup->positioner);
	}

	popup->geometry =
		wlr_xdg_positioner_get_geometry(&popup->positioner);

	return false;
}

static bool xdg_popup_unconstrain_slide(struct wlr_xdg_popup *popup,
		struct wlr_box *toplevel_sx_box) {
	int offset_x = 0, offset_y = 0;
	xdg_popup_box_constraints(popup, toplevel_sx_box,
		&offset_x, &offset_y);

	if (!offset_x && !offset_y) {
		return true;
	}

	bool slide_x = offset_x &&
		(popup->positioner.constraint_adjustment &
		 XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_SLIDE_X);

	bool slide_y = offset_x &&
		(popup->positioner.constraint_adjustment &
		 XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_SLIDE_Y);

	if (slide_x) {
		popup->geometry.x += offset_x;
	}

	if (slide_y) {
		popup->geometry.y += offset_y;
	}

	int toplevel_x = 0, toplevel_y = 0;
	wlr_xdg_popup_get_toplevel_coords(popup, popup->geometry.x,
		popup->geometry.y, &toplevel_x, &toplevel_y);

	if (slide_x && toplevel_x < toplevel_sx_box->x) {
		popup->geometry.x += toplevel_sx_box->x - toplevel_x;
	}
	if (slide_y && toplevel_y < toplevel_sx_box->y) {
		popup->geometry.y += toplevel_sx_box->y - toplevel_y;
	}

	xdg_popup_box_constraints(popup, toplevel_sx_box,
		&offset_x, &offset_y);

	return !offset_x && !offset_y;
}

static bool xdg_popup_unconstrain_resize(struct wlr_xdg_popup *popup,
		struct wlr_box *toplevel_sx_box) {
	int offset_x, offset_y;
	xdg_popup_box_constraints(popup, toplevel_sx_box,
		&offset_x, &offset_y);

	if (!offset_x && !offset_y) {
		return true;
	}

	bool resize_x = offset_x &&
		(popup->positioner.constraint_adjustment &
		 XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_RESIZE_X);

	bool resize_y = offset_x &&
		(popup->positioner.constraint_adjustment &
		 XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_RESIZE_Y);

	if (resize_x) {
		popup->geometry.width -= offset_x;
	}
	if (resize_y) {
		popup->geometry.height -= offset_y;
	}

	xdg_popup_box_constraints(popup, toplevel_sx_box,
		&offset_y, &offset_y);

	return !offset_x && !offset_y;
}

void wlr_xdg_popup_unconstrain_from_box(struct wlr_xdg_popup *popup,
		struct wlr_box *toplevel_sx_box) {
	if (xdg_popup_unconstrain_flip(popup, toplevel_sx_box)) {
		return;
	}
	if (xdg_popup_unconstrain_slide(popup, toplevel_sx_box)) {
		return;
	}
	if (xdg_popup_unconstrain_resize(popup, toplevel_sx_box)) {
		return;
	}
}

static enum xdg_positioner_anchor positioner_anchor_invert_x(
		enum xdg_positioner_anchor anchor) {
	switch (anchor) {
	case XDG_POSITIONER_ANCHOR_LEFT:
		return XDG_POSITIONER_ANCHOR_RIGHT;
	case XDG_POSITIONER_ANCHOR_RIGHT:
		return XDG_POSITIONER_ANCHOR_LEFT;
	case XDG_POSITIONER_ANCHOR_TOP_LEFT:
		return XDG_POSITIONER_ANCHOR_TOP_RIGHT;
	case XDG_POSITIONER_ANCHOR_BOTTOM_LEFT:
		return XDG_POSITIONER_ANCHOR_BOTTOM_RIGHT;
	case XDG_POSITIONER_ANCHOR_BOTTOM_RIGHT:
		return XDG_POSITIONER_ANCHOR_BOTTOM_LEFT;
	default:
		return anchor;
	}
}

static enum xdg_positioner_gravity positioner_gravity_invert_x(
		enum xdg_positioner_gravity gravity) {
	// gravity and edge enums are the same
	return (enum xdg_positioner_gravity)positioner_anchor_invert_x(
		(enum xdg_positioner_anchor)gravity);
}

static enum xdg_positioner_anchor positioner_anchor_invert_y(
		enum xdg_positioner_anchor anchor) {
	switch (anchor) {
	case XDG_POSITIONER_ANCHOR_TOP:
		return XDG_POSITIONER_ANCHOR_BOTTOM;
	case XDG_POSITIONER_ANCHOR_BOTTOM:
		return XDG_POSITIONER_ANCHOR_TOP;
	case XDG_POSITIONER_ANCHOR_TOP_LEFT:
		return XDG_POSITIONER_ANCHOR_BOTTOM_LEFT;
	case XDG_POSITIONER_ANCHOR_BOTTOM_LEFT:
		return XDG_POSITIONER_ANCHOR_TOP_LEFT;
	case XDG_POSITIONER_ANCHOR_BOTTOM_RIGHT:
		return XDG_POSITIONER_ANCHOR_TOP_RIGHT;
	default:
		return anchor;
	}
}

static enum xdg_positioner_gravity positioner_gravity_invert_y(
		enum xdg_positioner_gravity gravity) {
	// gravity and edge enums are the same
	return (enum xdg_positioner_gravity)positioner_anchor_invert_y(
		(enum xdg_positioner_anchor)gravity);
}

void wlr_positioner_invert_x(struct wlr_xdg_positioner *positioner) {
	positioner->anchor = positioner_anchor_invert_x(positioner->anchor);
	positioner->gravity = positioner_gravity_invert_x(positioner->gravity);
}

void wlr_positioner_invert_y(struct wlr_xdg_positioner *positioner) {
	positioner->anchor = positioner_anchor_invert_y(positioner->anchor);
	positioner->gravity = positioner_gravity_invert_y(positioner->gravity);
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

static void xdg_surface_for_each_surface(struct wlr_xdg_surface *surface,
		int x, int y, wlr_surface_iterator_func_t iterator, void *user_data) {
	struct xdg_surface_iterator_data data = {
		.user_iterator = iterator,
		.user_data = user_data,
		.x = x, .y = y,
	};
	wlr_surface_for_each_surface(surface->surface, xdg_surface_iterator,
		&data);

	struct wlr_xdg_popup *popup_state;
	wl_list_for_each(popup_state, &surface->popups, link) {
		struct wlr_xdg_surface *popup = popup_state->base;
		if (!popup->configured) {
			continue;
		}

		double popup_sx, popup_sy;
		wlr_xdg_surface_popup_get_position(popup, &popup_sx, &popup_sy);

		xdg_surface_for_each_surface(popup,
			x + popup_sx,
			y + popup_sy,
			iterator, user_data);
	}
}

void wlr_xdg_surface_for_each_surface(struct wlr_xdg_surface *surface,
		wlr_surface_iterator_func_t iterator, void *user_data) {
	xdg_surface_for_each_surface(surface, 0, 0, iterator, user_data);
}
