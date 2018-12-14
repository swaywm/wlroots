#define _XOPEN_SOURCE 700
#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <wlr/types/wlr_region.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/util/edges.h>
#include <wlr/util/log.h>
#include <wlr/util/region.h>
#ifdef __linux__
#include <linux/input-event-codes.h>
#elif __FreeBSD__
#include <dev/evdev/input-event-codes.h>
#endif
#include "rootston/cursor.h"
#include "rootston/desktop.h"
#include "rootston/view.h"
#include "rootston/xcursor.h"

struct roots_cursor *roots_cursor_create(struct roots_seat *seat) {
	struct roots_cursor *cursor = calloc(1, sizeof(struct roots_cursor));
	if (!cursor) {
		return NULL;
	}
	cursor->cursor = wlr_cursor_create();
	if (!cursor->cursor) {
		free(cursor);
		return NULL;
	}
	cursor->default_xcursor = ROOTS_XCURSOR_DEFAULT;
	return cursor;
}

void roots_cursor_destroy(struct roots_cursor *cursor) {
	// TODO
}

static void seat_view_deco_motion(struct roots_seat_view *view, double deco_sx, double deco_sy) {
	struct roots_cursor *cursor = view->seat->cursor;

	double sx = deco_sx;
	double sy = deco_sy;
	if (view->has_button_grab) {
		sx = view->grab_sx;
		sy = view->grab_sy;
	}

	enum roots_deco_part parts = view_get_deco_part(view->view, sx, sy);

	bool is_titlebar = (parts & ROOTS_DECO_PART_TITLEBAR);
	uint32_t edges = 0;
	if (parts & ROOTS_DECO_PART_LEFT_BORDER) {
		edges |= WLR_EDGE_LEFT;
	} else if (parts & ROOTS_DECO_PART_RIGHT_BORDER) {
		edges |= WLR_EDGE_RIGHT;
	} else if (parts & ROOTS_DECO_PART_BOTTOM_BORDER) {
		edges |= WLR_EDGE_BOTTOM;
	} else if (parts & ROOTS_DECO_PART_TOP_BORDER) {
		edges |= WLR_EDGE_TOP;
	}

	if (view->has_button_grab) {
		if (is_titlebar) {
			roots_seat_begin_move(view->seat, view->view);
		} else if (edges) {
			roots_seat_begin_resize(view->seat, view->view, edges);
		}
		view->has_button_grab = false;
	} else {
		if (is_titlebar) {
			wlr_xcursor_manager_set_cursor_image(cursor->xcursor_manager,
				cursor->default_xcursor, cursor->cursor);
		} else if (edges) {
			const char *resize_name = wlr_xcursor_get_resize_name(edges);
			wlr_xcursor_manager_set_cursor_image(cursor->xcursor_manager,
				resize_name, cursor->cursor);
		}
	}
}

static void seat_view_deco_leave(struct roots_seat_view *view) {
	struct roots_cursor *cursor = view->seat->cursor;
	wlr_xcursor_manager_set_cursor_image(cursor->xcursor_manager,
		cursor->default_xcursor, cursor->cursor);
	view->has_button_grab = false;
}

static void seat_view_deco_button(struct roots_seat_view *view, double sx,
		double sy, uint32_t button, uint32_t state) {
	if (button == BTN_LEFT && state == WLR_BUTTON_PRESSED) {
		view->has_button_grab = true;
		view->grab_sx = sx;
		view->grab_sy = sy;
	} else {
		view->has_button_grab = false;
	}

	enum roots_deco_part parts = view_get_deco_part(view->view, sx, sy);
	if (state == WLR_BUTTON_RELEASED && (parts & ROOTS_DECO_PART_TITLEBAR)) {
		struct roots_cursor *cursor = view->seat->cursor;
		wlr_xcursor_manager_set_cursor_image(cursor->xcursor_manager,
				cursor->default_xcursor, cursor->cursor);
	}
}

static void roots_passthrough_cursor(struct roots_cursor *cursor,
		int64_t time) {
	bool focus_changed;
	double sx, sy;
	struct roots_view *view = NULL;
	struct roots_seat *seat = cursor->seat;
	struct roots_desktop *desktop = seat->input->server->desktop;
	struct wlr_surface *surface = desktop_surface_at(desktop,
			cursor->cursor->x, cursor->cursor->y, &sx, &sy, &view);

	struct wl_client *client = NULL;
	if (surface) {
		client = wl_resource_get_client(surface->resource);
	}

	if (surface && !roots_seat_allow_input(seat, surface->resource)) {
		return;
	}

	if (cursor->cursor_client != client) {
		wlr_xcursor_manager_set_cursor_image(cursor->xcursor_manager,
			cursor->default_xcursor, cursor->cursor);
		cursor->cursor_client = client;
	}

	if (view) {
		struct roots_seat_view *seat_view =
			roots_seat_view_from_view(seat, view);

		if (cursor->pointer_view &&
				!cursor->wlr_surface && (surface || seat_view != cursor->pointer_view)) {
			seat_view_deco_leave(cursor->pointer_view);
		}

		cursor->pointer_view = seat_view;

		if (!surface) {
			seat_view_deco_motion(seat_view, sx, sy);
		}
	} else {
		cursor->pointer_view = NULL;
	}

	cursor->wlr_surface = surface;

	if (surface) {
		focus_changed = (seat->seat->pointer_state.focused_surface != surface);
		wlr_seat_pointer_notify_enter(seat->seat, surface, sx, sy);
		if (!focus_changed && time > 0) {
			wlr_seat_pointer_notify_motion(seat->seat, time, sx, sy);
		}
	} else {
		wlr_seat_pointer_clear_focus(seat->seat);
	}

	struct roots_drag_icon *drag_icon;
	wl_list_for_each(drag_icon, &seat->drag_icons, link) {
		roots_drag_icon_update_position(drag_icon);
	}
}

void roots_cursor_update_focus(struct roots_cursor *cursor) {
	roots_passthrough_cursor(cursor, -1);
}

void roots_cursor_update_position(struct roots_cursor *cursor,
		uint32_t time) {
	struct roots_seat *seat = cursor->seat;
	struct roots_view *view;
	switch (cursor->mode) {
	case ROOTS_CURSOR_PASSTHROUGH:
		roots_passthrough_cursor(cursor, time);
		break;
	case ROOTS_CURSOR_MOVE:
		view = roots_seat_get_focus(seat);
		if (view != NULL) {
			double dx = cursor->cursor->x - cursor->offs_x;
			double dy = cursor->cursor->y - cursor->offs_y;
			view_move(view, cursor->view_x + dx,
				cursor->view_y + dy);
		}
		break;
	case ROOTS_CURSOR_RESIZE:
		view = roots_seat_get_focus(seat);
		if (view != NULL) {
			double dx = cursor->cursor->x - cursor->offs_x;
			double dy = cursor->cursor->y - cursor->offs_y;
			double x = view->box.x;
			double y = view->box.y;
			int width = cursor->view_width;
			int height = cursor->view_height;
			if (cursor->resize_edges & WLR_EDGE_TOP) {
				y = cursor->view_y + dy;
				height -= dy;
				if (height < 1) {
					y += height;
				}
			} else if (cursor->resize_edges & WLR_EDGE_BOTTOM) {
				height += dy;
			}
			if (cursor->resize_edges & WLR_EDGE_LEFT) {
				x = cursor->view_x + dx;
				width -= dx;
				if (width < 1) {
					x += width;
				}
			} else if (cursor->resize_edges & WLR_EDGE_RIGHT) {
				width += dx;
			}
			view_move_resize(view, x, y,
					width < 1 ? 1 : width,
					height < 1 ? 1 : height);
		}
		break;
	case ROOTS_CURSOR_ROTATE:
		view = roots_seat_get_focus(seat);
		if (view != NULL) {
			int ox = view->box.x + view->wlr_surface->current.width/2,
				oy = view->box.y + view->wlr_surface->current.height/2;
			int ux = cursor->offs_x - ox,
				uy = cursor->offs_y - oy;
			int vx = cursor->cursor->x - ox,
				vy = cursor->cursor->y - oy;
			float angle = atan2(ux*vy - uy*vx, vx*ux + vy*uy);
			int steps = 12;
			angle = round(angle/M_PI*steps) / (steps/M_PI);
			view_rotate(view, cursor->view_rotation + angle);
		}
		break;
	}
}

static void roots_cursor_press_button(struct roots_cursor *cursor,
		struct wlr_input_device *device, uint32_t time, uint32_t button,
		uint32_t state, double lx, double ly) {
	struct roots_seat *seat = cursor->seat;
	struct roots_desktop *desktop = seat->input->server->desktop;

	bool is_touch = device->type == WLR_INPUT_DEVICE_TOUCH;

	double sx, sy;
	struct roots_view *view;
	struct wlr_surface *surface = desktop_surface_at(desktop,
			lx, ly, &sx, &sy, &view);

	if (state == WLR_BUTTON_PRESSED && view &&
			roots_seat_has_meta_pressed(seat)) {
		roots_seat_set_focus(seat, view);

		uint32_t edges;
		switch (button) {
		case BTN_LEFT:
			roots_seat_begin_move(seat, view);
			break;
		case BTN_RIGHT:
			edges = 0;
			if (sx < view->wlr_surface->current.width/2) {
				edges |= WLR_EDGE_LEFT;
			} else {
				edges |= WLR_EDGE_RIGHT;
			}
			if (sy < view->wlr_surface->current.height/2) {
				edges |= WLR_EDGE_TOP;
			} else {
				edges |= WLR_EDGE_BOTTOM;
			}
			roots_seat_begin_resize(seat, view, edges);
			break;
		case BTN_MIDDLE:
			roots_seat_begin_rotate(seat, view);
			break;
		}
	} else {
		if (view && !surface && cursor->pointer_view) {
			seat_view_deco_button(cursor->pointer_view,
				sx, sy, button, state);
		}

		if (state == WLR_BUTTON_RELEASED &&
				cursor->mode != ROOTS_CURSOR_PASSTHROUGH) {
			cursor->mode = ROOTS_CURSOR_PASSTHROUGH;
		}

		if (state == WLR_BUTTON_PRESSED) {
			if (view) {
				roots_seat_set_focus(seat, view);
			}
			if (surface && wlr_surface_is_layer_surface(surface)) {
				struct wlr_layer_surface_v1 *layer =
					wlr_layer_surface_v1_from_wlr_surface(surface);
				if (layer->current.keyboard_interactive) {
					roots_seat_set_focus_layer(seat, layer);
				}
			}
		}
	}

	if (!is_touch) {
		wlr_seat_pointer_notify_button(seat->seat, time, button, state);
	}
}

static void notify_relative_motion(struct roots_seat *seat, uint64_t time_msec,
		double dx, double dy, double dx_unaccel, double dy_unaccel) {

	struct wlr_relative_pointer_manager_v1 *relative_pointer_manager =
		seat->input->server->desktop->relative_pointer_manager;

	struct wlr_seat_client *client = seat->seat->pointer_state.focused_client;
	if (client == NULL) {
		return;
	}

	struct wl_resource *resource;
	wl_resource_for_each(resource, &relative_pointer_manager->relative_pointers) {

		struct wlr_relative_pointer_v1 *pointer =
			wlr_relative_pointer_v1_from_resource(resource);
		if (pointer == NULL) {
			continue;
		}

		wlr_relative_pointer_v1_send_relative_motion(pointer, time_msec,
				dx, dy, dx_unaccel, dy_unaccel);
		wl_pointer_send_frame(pointer->pointer);
	}

}

void roots_cursor_handle_motion(struct roots_cursor *cursor,
		struct wlr_event_pointer_motion *event) {
	double dx = event->delta_x;
	double dy = event->delta_y;

	notify_relative_motion(cursor->seat,
		(uint64_t)event->time_msec * 1000, dx, dy, dx, dy);

	if (cursor->active_constraint) {
		struct roots_view *view = cursor->pointer_view->view;
		assert(view);

		// TODO: handle rotated views
		if (view->rotation == 0.0) {
			double lx1 = cursor->cursor->x;
			double ly1 = cursor->cursor->y;

			double lx2 = lx1 + dx;
			double ly2 = ly1 + dy;

			double sx1 = lx1 - view->box.x;
			double sy1 = ly1 - view->box.y;

			double sx2 = lx2 - view->box.x;
			double sy2 = ly2 - view->box.y;

			double sx2_confined, sy2_confined;
			if (!wlr_region_confine(&cursor->confine, sx1, sy1, sx2, sy2,
					&sx2_confined, &sy2_confined)) {
				return;
			}

			dx = sx2_confined - sx1;
			dy = sy2_confined - sy1;
		}
	}

	wlr_cursor_move(cursor->cursor, event->device, dx, dy);
	roots_cursor_update_position(cursor, event->time_msec);
}

void roots_cursor_handle_motion_absolute(struct roots_cursor *cursor,
		struct wlr_event_pointer_motion_absolute *event) {
	double lx, ly;
	wlr_cursor_absolute_to_layout_coords(cursor->cursor, event->device, event->x,
		event->y, &lx, &ly);

	double dx = lx - cursor->cursor->x;
	double dy = ly - cursor->cursor->y;

	notify_relative_motion(cursor->seat,
		(uint64_t)event->time_msec * 1000, dx, dy, dx, dy);

	if (cursor->pointer_view) {
		struct roots_view *view = cursor->pointer_view->view;

		if (cursor->active_constraint &&
				!pixman_region32_contains_point(&cursor->confine,
					floor(lx - view->box.x), floor(ly - view->box.y), NULL)) {
			return;
		}
	}

	wlr_cursor_warp_closest(cursor->cursor, event->device, lx, ly);
	roots_cursor_update_position(cursor, event->time_msec);
}

void roots_cursor_handle_button(struct roots_cursor *cursor,
		struct wlr_event_pointer_button *event) {
	roots_cursor_press_button(cursor, event->device, event->time_msec,
		event->button, event->state, cursor->cursor->x, cursor->cursor->y);
}

void roots_cursor_handle_axis(struct roots_cursor *cursor,
		struct wlr_event_pointer_axis *event) {
	wlr_seat_pointer_notify_axis(cursor->seat->seat, event->time_msec,
		event->orientation, event->delta, event->delta_discrete, event->source);
}

void roots_cursor_handle_touch_down(struct roots_cursor *cursor,
		struct wlr_event_touch_down *event) {
	struct roots_desktop *desktop = cursor->seat->input->server->desktop;
	double lx, ly;
	wlr_cursor_absolute_to_layout_coords(cursor->cursor, event->device,
		event->x, event->y, &lx, &ly);

	double sx, sy;
	struct wlr_surface *surface = desktop_surface_at(
		desktop, lx, ly, &sx, &sy, NULL);

	uint32_t serial = 0;
	if (surface && roots_seat_allow_input(cursor->seat, surface->resource)) {
		serial = wlr_seat_touch_notify_down(cursor->seat->seat, surface,
			event->time_msec, event->touch_id, sx, sy);
	}

	if (serial && wlr_seat_touch_num_points(cursor->seat->seat) == 1) {
		cursor->seat->touch_id = event->touch_id;
		cursor->seat->touch_x = lx;
		cursor->seat->touch_y = ly;
		roots_cursor_press_button(cursor, event->device, event->time_msec,
			BTN_LEFT, 1, lx, ly);
	}
}

void roots_cursor_handle_touch_up(struct roots_cursor *cursor,
		struct wlr_event_touch_up *event) {
	struct wlr_touch_point *point =
		wlr_seat_touch_get_point(cursor->seat->seat, event->touch_id);
	if (!point) {
		return;
	}

	if (wlr_seat_touch_num_points(cursor->seat->seat) == 1) {
		roots_cursor_press_button(cursor, event->device, event->time_msec,
			BTN_LEFT, 0, cursor->seat->touch_x, cursor->seat->touch_y);
	}

	wlr_seat_touch_notify_up(cursor->seat->seat, event->time_msec,
		event->touch_id);
}

void roots_cursor_handle_touch_motion(struct roots_cursor *cursor,
		struct wlr_event_touch_motion *event) {
	struct roots_desktop *desktop = cursor->seat->input->server->desktop;
	struct wlr_touch_point *point =
		wlr_seat_touch_get_point(cursor->seat->seat, event->touch_id);
	if (!point) {
		return;
	}

	double lx, ly;
	wlr_cursor_absolute_to_layout_coords(cursor->cursor, event->device,
		event->x, event->y, &lx, &ly);

	double sx, sy;
	struct wlr_surface *surface = desktop_surface_at(
			desktop, lx, ly, &sx, &sy, NULL);

	if (surface && roots_seat_allow_input(cursor->seat, surface->resource)) {
		wlr_seat_touch_point_focus(cursor->seat->seat, surface,
			event->time_msec, event->touch_id, sx, sy);
		wlr_seat_touch_notify_motion(cursor->seat->seat, event->time_msec,
			event->touch_id, sx, sy);
	} else {
		wlr_seat_touch_point_clear_focus(cursor->seat->seat, event->time_msec,
			event->touch_id);
	}

	if (event->touch_id == cursor->seat->touch_id) {
		cursor->seat->touch_x = lx;
		cursor->seat->touch_y = ly;
	}
}

void roots_cursor_handle_tool_axis(struct roots_cursor *cursor,
		struct wlr_event_tablet_tool_axis *event) {
	double x = NAN, y = NAN;
	if ((event->updated_axes & WLR_TABLET_TOOL_AXIS_X) &&
			(event->updated_axes & WLR_TABLET_TOOL_AXIS_Y)) {
		x = event->x;
		y = event->y;
	} else if ((event->updated_axes & WLR_TABLET_TOOL_AXIS_X)) {
		x = event->x;
	} else if ((event->updated_axes & WLR_TABLET_TOOL_AXIS_Y)) {
		y = event->y;
	}

	double lx, ly;
	wlr_cursor_absolute_to_layout_coords(cursor->cursor, event->device,
		x, y, &lx, &ly);


	if (cursor->pointer_view) {
		struct roots_view *view = cursor->pointer_view->view;

		if (cursor->active_constraint &&
				!pixman_region32_contains_point(&cursor->confine,
					floor(lx - view->box.x), floor(ly - view->box.y), NULL)) {
			return;
		}
	}

	wlr_cursor_warp_closest(cursor->cursor, event->device, lx, ly);
	roots_cursor_update_position(cursor, event->time_msec);
}

void roots_cursor_handle_tool_tip(struct roots_cursor *cursor,
		struct wlr_event_tablet_tool_tip *event) {
	roots_cursor_press_button(cursor, event->device,
		event->time_msec, BTN_LEFT, event->state, cursor->cursor->x,
		cursor->cursor->y);
}

void roots_cursor_handle_request_set_cursor(struct roots_cursor *cursor,
		struct wlr_seat_pointer_request_set_cursor_event *event) {
	struct wlr_surface *focused_surface =
		event->seat_client->seat->pointer_state.focused_surface;
	bool has_focused =
		focused_surface != NULL && focused_surface->resource != NULL;
	struct wl_client *focused_client = NULL;
	if (has_focused) {
		focused_client = wl_resource_get_client(focused_surface->resource);
	}
	if (event->seat_client->client != focused_client ||
			cursor->mode != ROOTS_CURSOR_PASSTHROUGH) {
		wlr_log(WLR_DEBUG, "Denying request to set cursor from unfocused client");
		return;
	}

	wlr_cursor_set_surface(cursor->cursor, event->surface, event->hotspot_x,
		event->hotspot_y);
	cursor->cursor_client = event->seat_client->client;
}

void roots_cursor_handle_focus_change(struct roots_cursor *cursor,
		struct wlr_seat_pointer_focus_change_event *event) {
	double sx = event->sx;
	double sy = event->sy;

	double lx = cursor->cursor->x;
	double ly = cursor->cursor->y;

	wlr_log(WLR_DEBUG, "entered surface %p, lx: %f, ly: %f, sx: %f, sy: %f",
		event->new_surface, lx, ly, sx, sy);

	roots_cursor_constrain(cursor,
		wlr_pointer_constraints_v1_constraint_for_surface(
			cursor->seat->input->server->desktop->pointer_constraints,
			event->new_surface, cursor->seat->seat),
		sx, sy);
}

void roots_cursor_handle_constraint_commit(struct roots_cursor *cursor) {
	struct roots_desktop *desktop = cursor->seat->input->server->desktop;

	struct roots_view *view;
	double sx, sy;
	struct wlr_surface *surface = desktop_surface_at(desktop,
			cursor->cursor->x, cursor->cursor->y, &sx, &sy, &view);
	// This should never happen but views move around right when they're
	// created from (0, 0) to their actual coordinates.
	if (surface != cursor->active_constraint->surface) {
		roots_cursor_update_focus(cursor);
	} else {
		roots_cursor_constrain(cursor, cursor->active_constraint, sx, sy);
	}
}

static void handle_constraint_commit(struct wl_listener *listener,
		void *data) {
	struct roots_cursor *cursor =
		wl_container_of(listener, cursor, constraint_commit);
	assert(cursor->active_constraint->surface == data);
	roots_cursor_handle_constraint_commit(cursor);
}

void roots_cursor_constrain(struct roots_cursor *cursor,
		struct wlr_pointer_constraint_v1 *constraint, double sx, double sy) {
	if (cursor->active_constraint == constraint) {
		return;
	}

	wlr_log(WLR_DEBUG, "roots_cursor_constrain(%p, %p)",
		cursor, constraint);
	wlr_log(WLR_DEBUG, "cursor->active_constraint: %p",
		cursor->active_constraint);

	wl_list_remove(&cursor->constraint_commit.link);
	wl_list_init(&cursor->constraint_commit.link);
	if (cursor->active_constraint) {
		wlr_pointer_constraint_v1_send_deactivated(
			cursor->active_constraint);
	}

	cursor->active_constraint = constraint;

	if (constraint == NULL) {
		return;
	}

	wlr_pointer_constraint_v1_send_activated(constraint);

	wl_list_remove(&cursor->constraint_commit.link);
	wl_signal_add(&constraint->surface->events.commit,
		&cursor->constraint_commit);
	cursor->constraint_commit.notify = handle_constraint_commit;

	pixman_region32_clear(&cursor->confine);

	pixman_region32_t *region = &constraint->region;

	if (!pixman_region32_contains_point(region, floor(sx), floor(sy), NULL)) {
		// Warp into region if possible
		int nboxes;
		pixman_box32_t *boxes = pixman_region32_rectangles(region, &nboxes);
		if (nboxes > 0) {
			struct roots_view *view = cursor->pointer_view->view;

			double sx = (boxes[0].x1 + boxes[0].x2) / 2.;
			double sy = (boxes[0].y1 + boxes[0].y2) / 2.;

			rotate_child_position(&sx, &sy, 0, 0, view->box.width, view->box.height,
				view->rotation);

			double lx = view->box.x + sx;
			double ly = view->box.y + sy;

			wlr_cursor_warp_closest(cursor->cursor, NULL, lx, ly);
		}
	}

	// A locked pointer will result in an empty region, thus disallowing all movement
	if (constraint->type == WLR_POINTER_CONSTRAINT_V1_CONFINED) {
		pixman_region32_copy(&cursor->confine, region);
	}
}
