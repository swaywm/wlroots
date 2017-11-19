#define _XOPEN_SOURCE 700
#include <stdlib.h>
#include <math.h>
#ifdef __linux__
#include <linux/input-event-codes.h>
#elif __FreeBSD__
#include <dev/evdev/input-event-codes.h>
#endif
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/util/log.h>
#include "rootston/xcursor.h"
#include "rootston/cursor.h"

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
	return cursor;
}

void roots_cursor_destroy(struct roots_cursor *cursor) {
	// TODO
}

static void roots_cursor_update_position(struct roots_cursor *cursor,
		uint32_t time) {
	struct roots_desktop *desktop = cursor->seat->input->server->desktop;
	struct roots_seat *seat = cursor->seat;
	struct roots_view *view;
	struct wlr_surface *surface;
	double sx, sy;
	switch (cursor->mode) {
	case ROOTS_CURSOR_PASSTHROUGH:
		view = view_at(desktop, cursor->cursor->x, cursor->cursor->y,
			&surface, &sx, &sy);
		bool set_compositor_cursor = !view && cursor->cursor_client;
		if (view) {
			struct wl_client *view_client =
				wl_resource_get_client(view->wlr_surface->resource);
			set_compositor_cursor = view_client != cursor->cursor_client;
		}
		if (set_compositor_cursor) {
			wlr_xcursor_manager_set_cursor_image(cursor->xcursor_manager,
				ROOTS_XCURSOR_DEFAULT, cursor->cursor);
			cursor->cursor_client = NULL;
		}
		if (view) {
			wlr_seat_pointer_notify_enter(seat->seat, surface, sx, sy);
			wlr_seat_pointer_notify_motion(seat->seat, time, sx, sy);
		} else {
			wlr_seat_pointer_clear_focus(seat->seat);
		}
		break;
	case ROOTS_CURSOR_MOVE:
		if (seat->focus) {
			double dx = cursor->cursor->x - cursor->offs_x;
			double dy = cursor->cursor->y - cursor->offs_y;
			view_move(seat->focus->view, cursor->view_x + dx,
				cursor->view_y + dy);
		}
		break;
	case ROOTS_CURSOR_RESIZE:
		if (seat->focus) {
			double dx = cursor->cursor->x - cursor->offs_x;
			double dy = cursor->cursor->y - cursor->offs_y;
			double active_x = seat->focus->view->x;
			double active_y = seat->focus->view->y;
			int width = cursor->view_width;
			int height = cursor->view_height;
			if (cursor->resize_edges & ROOTS_CURSOR_RESIZE_EDGE_TOP) {
				active_y = cursor->view_y + dy;
				height -= dy;
				if (height < 0) {
					active_y += height;
				}
			} else if (cursor->resize_edges & ROOTS_CURSOR_RESIZE_EDGE_BOTTOM) {
				height += dy;
			}
			if (cursor->resize_edges & ROOTS_CURSOR_RESIZE_EDGE_LEFT) {
				active_x = cursor->view_x + dx;
				width -= dx;
				if (width < 0) {
					active_x += width;
				}
			} else if (cursor->resize_edges & ROOTS_CURSOR_RESIZE_EDGE_RIGHT) {
				width += dx;
			}

			if (width < 0) {
				width = 0;
			}
			if (height < 0) {
				height = 0;
			}

			if (active_x != seat->focus->view->x ||
					active_y != seat->focus->view->y) {
				view_move_resize(seat->focus->view, active_x, active_y,
					width, height);
			} else {
				view_resize(seat->focus->view, width, height);
			}
		}
		break;
	case ROOTS_CURSOR_ROTATE:
		if (seat->focus) {
			struct roots_view *view = seat->focus->view;
			int ox = view->x + view->wlr_surface->current->width/2,
				oy = view->y + view->wlr_surface->current->height/2;
			int ux = cursor->offs_x - ox,
				uy = cursor->offs_y - oy;
			int vx = cursor->cursor->x - ox,
				vy = cursor->cursor->y - oy;
			float angle = atan2(vx*uy - vy*ux, vx*ux + vy*uy);
			int steps = 12;
			angle = round(angle/M_PI*steps) / (steps/M_PI);
			view->rotation = cursor->view_rotation + angle;
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

	struct wlr_surface *surface;
	double sx, sy;
	struct roots_view *view = view_at(desktop, lx, ly, &surface, &sx, &sy);

	if (state == WLR_BUTTON_PRESSED &&
			view &&
			roots_seat_has_meta_pressed(seat)) {
		roots_seat_focus_view(seat, view);

		uint32_t edges;
		switch (button) {
		case BTN_LEFT:
			roots_seat_begin_move(seat, view);
			break;
		case BTN_RIGHT:
			edges = 0;
			if (sx < view->wlr_surface->current->width/2) {
				edges |= ROOTS_CURSOR_RESIZE_EDGE_LEFT;
			} else {
				edges |= ROOTS_CURSOR_RESIZE_EDGE_RIGHT;
			}
			if (sy < view->wlr_surface->current->height/2) {
				edges |= ROOTS_CURSOR_RESIZE_EDGE_TOP;
			} else {
				edges |= ROOTS_CURSOR_RESIZE_EDGE_BOTTOM;
			}
			roots_seat_begin_resize(seat, view, edges);
			break;
		case BTN_MIDDLE:
			roots_seat_begin_rotate(seat, view);
			break;
		}
		return;
	}

	uint32_t serial;
	if (is_touch) {
		serial = wl_display_get_serial(desktop->server->wl_display);
	} else {
		serial =
			wlr_seat_pointer_notify_button(seat->seat, time, button, state);
	}

	int i;
	switch (state) {
	case WLR_BUTTON_RELEASED:
		seat->cursor->mode = ROOTS_CURSOR_PASSTHROUGH;
		if (!is_touch) {
			roots_cursor_update_position(cursor, time);
		}
		break;
	case WLR_BUTTON_PRESSED:
		i = cursor->input_events_idx;
		cursor->input_events[i].serial = serial;
		cursor->input_events[i].cursor = cursor->cursor;
		cursor->input_events[i].device = device;
		cursor->input_events_idx = (i + 1)
			% (sizeof(cursor->input_events) / sizeof(cursor->input_events[0]));
		roots_seat_focus_view(seat, view);
		break;
	}
}

void roots_cursor_handle_motion(struct roots_cursor *cursor,
		struct wlr_event_pointer_motion *event) {
	wlr_cursor_move(cursor->cursor, event->device,
			event->delta_x, event->delta_y);
	roots_cursor_update_position(cursor, event->time_msec);
}

void roots_cursor_handle_motion_absolute(struct roots_cursor *cursor,
		struct wlr_event_pointer_motion_absolute *event) {
	wlr_cursor_warp_absolute(cursor->cursor, event->device,
		event->x_mm / event->width_mm, event->y_mm / event->height_mm);
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
		event->orientation, event->delta);
}

void roots_cursor_handle_touch_down(struct roots_cursor *cursor,
		struct wlr_event_touch_down *event) {
	struct roots_desktop *desktop = cursor->seat->input->server->desktop;
	struct wlr_surface *surface = NULL;
	double lx, ly;
	bool result =
		wlr_cursor_absolute_to_layout_coords(cursor->cursor,
			event->device, event->x_mm, event->y_mm, event->width_mm,
			event->height_mm, &lx, &ly);
	if (!result) {
		return;
	}
	double sx, sy;
	view_at(desktop, lx, ly, &surface, &sx, &sy);

	uint32_t serial = 0;
	if (surface) {
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

	struct wlr_surface *surface = NULL;
	double lx, ly;
	bool result =
		wlr_cursor_absolute_to_layout_coords(cursor->cursor,
			event->device, event->x_mm, event->y_mm, event->width_mm,
			event->height_mm, &lx, &ly);
	if (!result) {
		return;
	}

	double sx, sy;
	view_at(desktop, lx, ly, &surface, &sx, &sy);

	if (surface) {
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
	if ((event->updated_axes & WLR_TABLET_TOOL_AXIS_X) &&
			(event->updated_axes & WLR_TABLET_TOOL_AXIS_Y)) {
		wlr_cursor_warp_absolute(cursor->cursor, event->device,
			event->x_mm / event->width_mm, event->y_mm / event->height_mm);
		roots_cursor_update_position(cursor, event->time_msec);
	} else if ((event->updated_axes & WLR_TABLET_TOOL_AXIS_X)) {
		wlr_cursor_warp_absolute(cursor->cursor, event->device,
			event->x_mm / event->width_mm, -1);
		roots_cursor_update_position(cursor, event->time_msec);
	} else if ((event->updated_axes & WLR_TABLET_TOOL_AXIS_Y)) {
		wlr_cursor_warp_absolute(cursor->cursor, event->device,
			-1, event->y_mm / event->height_mm);
		roots_cursor_update_position(cursor, event->time_msec);
	}
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
		wlr_log(L_DEBUG, "Denying request to set cursor from unfocused client");
		return;
	}

	wlr_log(L_DEBUG, "Setting client cursor");
	wlr_cursor_set_surface(cursor->cursor, event->surface, event->hotspot_x,
		event->hotspot_y);
	cursor->cursor_client = event->seat_client->client;
}
