#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "types/wlr_xdg_shell_v6.h"
#include "util/signal.h"

static struct wlr_xdg_surface_v6 *xdg_popup_grab_get_topmost(
		struct wlr_xdg_popup_grab_v6 *grab) {
	struct wlr_xdg_popup_v6 *popup;
	wl_list_for_each(popup, &grab->popups, grab_link) {
		return popup->base;
	}

	return NULL;
}

static void xdg_popup_grab_end(struct wlr_xdg_popup_grab_v6 *popup_grab) {
	struct wlr_xdg_popup_v6 *popup, *tmp;
	wl_list_for_each_safe(popup, tmp, &popup_grab->popups, grab_link) {
		zxdg_popup_v6_send_popup_done(popup->resource);
	}

	wlr_seat_pointer_end_grab(popup_grab->seat);
	wlr_seat_keyboard_end_grab(popup_grab->seat);
	wlr_seat_touch_end_grab(popup_grab->seat);
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
		xdg_popup_grab_end(grab->data);
		return 0;
	}
}

static void xdg_pointer_grab_axis(struct wlr_seat_pointer_grab *grab,
		uint32_t time, enum wlr_axis_orientation orientation, double value,
		int32_t value_discrete, enum wlr_axis_source source) {
	wlr_seat_pointer_send_axis(grab->seat, time, orientation, value,
		value_discrete, source);
}

static void xdg_pointer_grab_frame(struct wlr_seat_pointer_grab *grab) {
	wlr_seat_pointer_send_frame(grab->seat);
}

static void xdg_pointer_grab_cancel(struct wlr_seat_pointer_grab *grab) {
	xdg_popup_grab_end(grab->data);
}

static const struct wlr_pointer_grab_interface xdg_pointer_grab_impl = {
	.enter = xdg_pointer_grab_enter,
	.motion = xdg_pointer_grab_motion,
	.button = xdg_pointer_grab_button,
	.cancel = xdg_pointer_grab_cancel,
	.axis = xdg_pointer_grab_axis,
	.frame = xdg_pointer_grab_frame,
};

static void xdg_keyboard_grab_enter(struct wlr_seat_keyboard_grab *grab,
		struct wlr_surface *surface, uint32_t keycodes[], size_t num_keycodes,
		struct wlr_keyboard_modifiers *modifiers) {
	// keyboard focus should remain on the popup
}

static void xdg_keyboard_grab_key(struct wlr_seat_keyboard_grab *grab,
		uint32_t time, uint32_t key, uint32_t state) {
	wlr_seat_keyboard_send_key(grab->seat, time, key, state);
}

static void xdg_keyboard_grab_modifiers(struct wlr_seat_keyboard_grab *grab,
		struct wlr_keyboard_modifiers *modifiers) {
	wlr_seat_keyboard_send_modifiers(grab->seat, modifiers);
}

static void xdg_keyboard_grab_cancel(struct wlr_seat_keyboard_grab *grab) {
	wlr_seat_pointer_end_grab(grab->seat);
}

static const struct wlr_keyboard_grab_interface xdg_keyboard_grab_impl = {
	.enter = xdg_keyboard_grab_enter,
	.key = xdg_keyboard_grab_key,
	.modifiers = xdg_keyboard_grab_modifiers,
	.cancel = xdg_keyboard_grab_cancel,
};

static uint32_t xdg_touch_grab_down(struct wlr_seat_touch_grab *grab,
		uint32_t time, struct wlr_touch_point *point) {
	struct wlr_xdg_popup_grab_v6 *popup_grab = grab->data;

	if (wl_resource_get_client(point->surface->resource) != popup_grab->client) {
		xdg_popup_grab_end(grab->data);
		return 0;
	}

	return wlr_seat_touch_send_down(grab->seat, point->surface, time,
			point->touch_id, point->sx, point->sy);
}

static void xdg_touch_grab_up(struct wlr_seat_touch_grab *grab,
		uint32_t time, struct wlr_touch_point *point) {
	wlr_seat_touch_send_up(grab->seat, time, point->touch_id);
}

static void xdg_touch_grab_motion(struct wlr_seat_touch_grab *grab,
		uint32_t time, struct wlr_touch_point *point) {
	wlr_seat_touch_send_motion(grab->seat, time, point->touch_id, point->sx,
		point->sy);
}

static void xdg_touch_grab_enter(struct wlr_seat_touch_grab *grab,
		uint32_t time, struct wlr_touch_point *point) {
}

static void xdg_touch_grab_cancel(struct wlr_seat_touch_grab *grab) {
	wlr_seat_touch_end_grab(grab->seat);
}

static const struct wlr_touch_grab_interface xdg_touch_grab_impl = {
	.down = xdg_touch_grab_down,
	.up = xdg_touch_grab_up,
	.motion = xdg_touch_grab_motion,
	.enter = xdg_touch_grab_enter,
	.cancel = xdg_touch_grab_cancel
};

static void xdg_popup_grab_handle_seat_destroy(
		struct wl_listener *listener, void *data) {
	struct wlr_xdg_popup_grab_v6 *xdg_grab =
		wl_container_of(listener, xdg_grab, seat_destroy);

	wl_list_remove(&xdg_grab->seat_destroy.link);

	struct wlr_xdg_popup_v6 *popup, *next;
	wl_list_for_each_safe(popup, next, &xdg_grab->popups, grab_link) {
		destroy_xdg_surface_v6(popup->base);
	}

	wl_list_remove(&xdg_grab->link);
	free(xdg_grab);
}

struct wlr_xdg_popup_grab_v6 *get_xdg_shell_v6_popup_grab_from_seat(
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
	xdg_grab->touch_grab.data = xdg_grab;
	xdg_grab->touch_grab.interface = &xdg_touch_grab_impl;

	wl_list_init(&xdg_grab->popups);

	wl_list_insert(&shell->popup_grabs, &xdg_grab->link);
	xdg_grab->seat = seat;

	xdg_grab->seat_destroy.notify = xdg_popup_grab_handle_seat_destroy;
	wl_signal_add(&seat->events.destroy, &xdg_grab->seat_destroy);

	return xdg_grab;
}


static const struct zxdg_popup_v6_interface zxdg_popup_v6_implementation;

static struct wlr_xdg_surface_v6 *xdg_surface_from_xdg_popup_resource(
		struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource, &zxdg_popup_v6_interface,
		&zxdg_popup_v6_implementation));
	return wl_resource_get_user_data(resource);
}

void destroy_xdg_popup_v6(struct wlr_xdg_surface_v6 *surface) {
	assert(surface->role == WLR_XDG_SURFACE_V6_ROLE_POPUP);
	unmap_xdg_surface_v6(surface);

	wl_resource_set_user_data(surface->popup->resource, NULL);
	wl_list_remove(&surface->popup->link);
	free(surface->popup);
	surface->popup = NULL;

	surface->role = WLR_XDG_SURFACE_V6_ROLE_NONE;
}

static void xdg_popup_handle_grab(struct wl_client *client,
		struct wl_resource *resource, struct wl_resource *seat_resource,
		uint32_t serial) {
	struct wlr_xdg_surface_v6 *surface =
		xdg_surface_from_xdg_popup_resource(resource);
	struct wlr_seat_client *seat_client =
		wlr_seat_client_from_resource(seat_resource);
	if (!surface) {
		return;
	}

	if (surface->popup->committed) {
		wl_resource_post_error(surface->popup->resource,
			ZXDG_POPUP_V6_ERROR_INVALID_GRAB,
			"xdg_popup is already mapped");
		return;
	}

	struct wlr_xdg_popup_grab_v6 *popup_grab =
		get_xdg_shell_v6_popup_grab_from_seat(surface->client->shell,
			seat_client->seat);

	struct wlr_xdg_surface_v6 *topmost = xdg_popup_grab_get_topmost(popup_grab);
	bool parent_is_toplevel =
		surface->popup->parent->role == WLR_XDG_SURFACE_V6_ROLE_TOPLEVEL;

	if ((topmost == NULL && !parent_is_toplevel) ||
			(topmost != NULL && topmost != surface->popup->parent)) {
		wl_resource_post_error(surface->client->resource,
			ZXDG_SHELL_V6_ERROR_NOT_THE_TOPMOST_POPUP,
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
	wlr_seat_touch_start_grab(seat_client->seat,
		&popup_grab->touch_grab);
}

static void xdg_popup_handle_destroy(struct wl_client *client,
		struct wl_resource *resource) {
	struct wlr_xdg_surface_v6 *surface =
		xdg_surface_from_xdg_popup_resource(resource);

	if (surface && !wl_list_empty(&surface->popups)) {
		wl_resource_post_error(surface->client->resource,
			ZXDG_SHELL_V6_ERROR_NOT_THE_TOPMOST_POPUP,
			"xdg_popup was destroyed while it was not the topmost popup");
		return;
	}

	wl_resource_destroy(resource);
}

static const struct zxdg_popup_v6_interface zxdg_popup_v6_implementation = {
	.destroy = xdg_popup_handle_destroy,
	.grab = xdg_popup_handle_grab,
};

static void xdg_popup_handle_resource_destroy(struct wl_resource *resource) {
	struct wlr_xdg_surface_v6 *surface =
		xdg_surface_from_xdg_popup_resource(resource);
	if (surface != NULL) {
		destroy_xdg_popup_v6(surface);
	}
}

void handle_xdg_surface_v6_popup_committed(struct wlr_xdg_surface_v6 *surface) {
	assert(surface->role == WLR_XDG_SURFACE_V6_ROLE_POPUP);

	if (!surface->popup->committed) {
		schedule_xdg_surface_v6_configure(surface);
		surface->popup->committed = true;
	}
}

const struct wlr_surface_role xdg_popup_v6_surface_role = {
	.name = "xdg_popup_v6",
	.commit = handle_xdg_surface_v6_commit,
	.precommit = handle_xdg_surface_v6_precommit,
};

void create_xdg_popup_v6(struct wlr_xdg_surface_v6 *xdg_surface,
		struct wlr_xdg_surface_v6 *parent,
		struct wlr_xdg_positioner_v6_resource *positioner, int32_t id) {
	if (positioner->attrs.size.width == 0 ||
			positioner->attrs.anchor_rect.width == 0) {
		wl_resource_post_error(xdg_surface->resource,
			ZXDG_SHELL_V6_ERROR_INVALID_POSITIONER,
			"positioner object is not complete");
		return;
	}

	if (!wlr_surface_set_role(xdg_surface->surface, &xdg_popup_v6_surface_role,
			xdg_surface, xdg_surface->resource, ZXDG_SHELL_V6_ERROR_ROLE)) {
		return;
	}

	xdg_surface->popup = calloc(1, sizeof(struct wlr_xdg_popup_v6));
	if (!xdg_surface->popup) {
		wl_resource_post_no_memory(xdg_surface->resource);
		return;
	}

	xdg_surface->popup->resource =
		wl_resource_create(xdg_surface->client->client, &zxdg_popup_v6_interface,
			wl_resource_get_version(xdg_surface->resource), id);
	if (xdg_surface->popup->resource == NULL) {
		free(xdg_surface->popup);
		wl_resource_post_no_memory(xdg_surface->resource);
		return;
	}
	wl_resource_set_implementation(xdg_surface->popup->resource,
		&zxdg_popup_v6_implementation, xdg_surface,
		xdg_popup_handle_resource_destroy);

	xdg_surface->role = WLR_XDG_SURFACE_V6_ROLE_POPUP;
	xdg_surface->popup->base = xdg_surface;
	xdg_surface->popup->parent = parent;
	xdg_surface->popup->geometry =
		wlr_xdg_positioner_v6_get_geometry(&positioner->attrs);

	// positioner properties
	memcpy(&xdg_surface->popup->positioner, &positioner->attrs,
		sizeof(struct wlr_xdg_positioner_v6));

	wl_list_insert(&parent->popups, &xdg_surface->popup->link);

	wlr_signal_emit_safe(&parent->events.new_popup, xdg_surface->popup);
}

void wlr_xdg_popup_v6_get_anchor_point(struct wlr_xdg_popup_v6 *popup,
		int *root_sx, int *root_sy) {
	struct wlr_box rect = popup->positioner.anchor_rect;
	enum zxdg_positioner_v6_anchor anchor = popup->positioner.anchor;
	int sx = 0, sy = 0;

	if (anchor == ZXDG_POSITIONER_V6_ANCHOR_NONE) {
		sx = (rect.x + rect.width) / 2;
		sy = (rect.y + rect.height) / 2;
	} else if (anchor == ZXDG_POSITIONER_V6_ANCHOR_TOP) {
		sx = (rect.x + rect.width) / 2;
		sy = rect.y;
	} else if (anchor == ZXDG_POSITIONER_V6_ANCHOR_BOTTOM) {
		sx = (rect.x + rect.width) / 2;
		sy = rect.y + rect.height;
	} else if (anchor == ZXDG_POSITIONER_V6_ANCHOR_LEFT) {
		sx = rect.x;
		sy = (rect.y + rect.height) / 2;
	} else if (anchor == ZXDG_POSITIONER_V6_ANCHOR_RIGHT) {
		sx = rect.x + rect.width;
		sy = (rect.y + rect.height) / 2;
	} else if (anchor == (ZXDG_POSITIONER_V6_ANCHOR_TOP |
				ZXDG_POSITIONER_V6_ANCHOR_LEFT)) {
		sx = rect.x;
		sy = rect.y;
	} else if (anchor == (ZXDG_POSITIONER_V6_ANCHOR_TOP |
				ZXDG_POSITIONER_V6_ANCHOR_RIGHT)) {
		sx = rect.x + rect.width;
		sy = rect.y;
	} else if (anchor == (ZXDG_POSITIONER_V6_ANCHOR_BOTTOM |
				ZXDG_POSITIONER_V6_ANCHOR_LEFT)) {
		sx = rect.x;
		sy = rect.y + rect.height;
	} else if (anchor == (ZXDG_POSITIONER_V6_ANCHOR_BOTTOM |
				ZXDG_POSITIONER_V6_ANCHOR_RIGHT)) {
		sx = rect.x + rect.width;
		sy = rect.y + rect.height;
	}

	*root_sx = sx;
	*root_sy = sy;
}

void wlr_xdg_popup_v6_get_toplevel_coords(struct wlr_xdg_popup_v6 *popup,
		int popup_sx, int popup_sy, int *toplevel_sx, int *toplevel_sy) {
	struct wlr_xdg_surface_v6 *parent = popup->parent;
	while (parent != NULL && parent->role == WLR_XDG_SURFACE_V6_ROLE_POPUP) {
		popup_sx += parent->popup->geometry.x;
		popup_sy += parent->popup->geometry.y;
		parent = parent->popup->parent;
	}
	assert(parent);

	*toplevel_sx = popup_sx + parent->geometry.x;
	*toplevel_sy = popup_sy + parent->geometry.y;
}

static void xdg_popup_v6_box_constraints(struct wlr_xdg_popup_v6 *popup,
		struct wlr_box *toplevel_sx_box, int *offset_x, int *offset_y) {
	int popup_width = popup->geometry.width;
	int popup_height = popup->geometry.height;
	int anchor_sx = 0, anchor_sy = 0;
	wlr_xdg_popup_v6_get_anchor_point(popup, &anchor_sx, &anchor_sy);
	int popup_sx = 0, popup_sy = 0;
	wlr_xdg_popup_v6_get_toplevel_coords(popup, popup->geometry.x,
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

static bool xdg_popup_v6_unconstrain_flip(struct wlr_xdg_popup_v6 *popup,
		struct wlr_box *toplevel_sx_box) {
	int offset_x = 0, offset_y = 0;
	xdg_popup_v6_box_constraints(popup, toplevel_sx_box,
		&offset_x, &offset_y);

	if (!offset_x && !offset_y) {
		return true;
	}

	bool flip_x = offset_x &&
		(popup->positioner.constraint_adjustment &
		 ZXDG_POSITIONER_V6_CONSTRAINT_ADJUSTMENT_FLIP_X);

	bool flip_y = offset_y &&
		(popup->positioner.constraint_adjustment &
		 ZXDG_POSITIONER_V6_CONSTRAINT_ADJUSTMENT_FLIP_Y);

	if (flip_x) {
		wlr_positioner_v6_invert_x(&popup->positioner);
	}
	if (flip_y) {
		wlr_positioner_v6_invert_y(&popup->positioner);
	}

	popup->geometry =
		wlr_xdg_positioner_v6_get_geometry(&popup->positioner);

	xdg_popup_v6_box_constraints(popup, toplevel_sx_box,
		&offset_x, &offset_y);

	if (!offset_x && !offset_y) {
		// no longer constrained
		return true;
	}

	// revert the positioner back if it didn't fix it and go to the next part
	if (flip_x) {
		wlr_positioner_v6_invert_x(&popup->positioner);
	}
	if (flip_y) {
		wlr_positioner_v6_invert_y(&popup->positioner);
	}

	popup->geometry =
		wlr_xdg_positioner_v6_get_geometry(&popup->positioner);

	return false;
}

static bool xdg_popup_v6_unconstrain_slide(struct wlr_xdg_popup_v6 *popup,
		struct wlr_box *toplevel_sx_box) {
	int offset_x = 0, offset_y = 0;
	xdg_popup_v6_box_constraints(popup, toplevel_sx_box,
		&offset_x, &offset_y);

	if (!offset_x && !offset_y) {
		return true;
	}

	bool slide_x = offset_x &&
		(popup->positioner.constraint_adjustment &
		 ZXDG_POSITIONER_V6_CONSTRAINT_ADJUSTMENT_SLIDE_X);

	bool slide_y = offset_y &&
		(popup->positioner.constraint_adjustment &
		 ZXDG_POSITIONER_V6_CONSTRAINT_ADJUSTMENT_SLIDE_Y);

	if (slide_x) {
		popup->geometry.x += offset_x;
	}

	if (slide_y) {
		popup->geometry.y += offset_y;
	}

	int toplevel_x = 0, toplevel_y = 0;
	wlr_xdg_popup_v6_get_toplevel_coords(popup, popup->geometry.x,
		popup->geometry.y, &toplevel_x, &toplevel_y);

	if (slide_x && toplevel_x < toplevel_sx_box->x) {
		popup->geometry.x += toplevel_sx_box->x - toplevel_x;
	}
	if (slide_y && toplevel_y < toplevel_sx_box->y) {
		popup->geometry.y += toplevel_sx_box->y - toplevel_y;
	}

	xdg_popup_v6_box_constraints(popup, toplevel_sx_box,
		&offset_x, &offset_y);

	return !offset_x && !offset_y;
}

static bool xdg_popup_v6_unconstrain_resize(struct wlr_xdg_popup_v6 *popup,
		struct wlr_box *toplevel_sx_box) {
	int offset_x, offset_y;
	xdg_popup_v6_box_constraints(popup, toplevel_sx_box,
		&offset_x, &offset_y);

	if (!offset_x && !offset_y) {
		return true;
	}

	bool resize_x = offset_x &&
		(popup->positioner.constraint_adjustment &
		 ZXDG_POSITIONER_V6_CONSTRAINT_ADJUSTMENT_RESIZE_X);

	bool resize_y = offset_y &&
		(popup->positioner.constraint_adjustment &
		 ZXDG_POSITIONER_V6_CONSTRAINT_ADJUSTMENT_RESIZE_Y);

	if (resize_x) {
		popup->geometry.width -= offset_x;
	}
	if (resize_y) {
		popup->geometry.height -= offset_y;
	}

	xdg_popup_v6_box_constraints(popup, toplevel_sx_box,
		&offset_x, &offset_y);

	return !offset_x && !offset_y;
}

void wlr_xdg_popup_v6_unconstrain_from_box(struct wlr_xdg_popup_v6 *popup,
		struct wlr_box *toplevel_sx_box) {
	if (xdg_popup_v6_unconstrain_flip(popup, toplevel_sx_box)) {
		return;
	}
	if (xdg_popup_v6_unconstrain_slide(popup, toplevel_sx_box)) {
		return;
	}
	if (xdg_popup_v6_unconstrain_resize(popup, toplevel_sx_box)) {
		return;
	}
}
