#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <wayland-server.h>
#include <wlr/config.h>
#include <wlr/types/wlr_box.h>
#include <wlr/types/wlr_surface.h>
#include <wlr/util/log.h>
#include <wlr/xwayland.h>
#include "rootston/server.h"
#include "rootston/server.h"

static void activate(struct roots_view *view, bool active) {
	assert(view->type == ROOTS_XWAYLAND_VIEW);
	wlr_xwayland_surface_activate(view->xwayland_surface, active);
}

static void move(struct roots_view *view, double x, double y) {
	assert(view->type == ROOTS_XWAYLAND_VIEW);
	struct wlr_xwayland_surface *xwayland_surface = view->xwayland_surface;
	view_update_position(view, x, y);
	wlr_xwayland_surface_configure(xwayland_surface, x, y,
		xwayland_surface->width, xwayland_surface->height);
}

static void apply_size_constraints(
		struct wlr_xwayland_surface *xwayland_surface, uint32_t width,
		uint32_t height, uint32_t *dest_width, uint32_t *dest_height) {
	*dest_width = width;
	*dest_height = height;

	struct wlr_xwayland_surface_size_hints *size_hints =
		xwayland_surface->size_hints;
	if (size_hints != NULL) {
		if (width < (uint32_t)size_hints->min_width) {
			*dest_width = size_hints->min_width;
		} else if (size_hints->max_width > 0 &&
				width > (uint32_t)size_hints->max_width) {
			*dest_width = size_hints->max_width;
		}
		if (height < (uint32_t)size_hints->min_height) {
			*dest_height = size_hints->min_height;
		} else if (size_hints->max_height > 0 &&
				height > (uint32_t)size_hints->max_height) {
			*dest_height = size_hints->max_height;
		}
	}
}

static void resize(struct roots_view *view, uint32_t width, uint32_t height) {
	assert(view->type == ROOTS_XWAYLAND_VIEW);
	struct wlr_xwayland_surface *xwayland_surface = view->xwayland_surface;

	uint32_t constrained_width, constrained_height;
	apply_size_constraints(xwayland_surface, width, height, &constrained_width,
		&constrained_height);

	wlr_xwayland_surface_configure(xwayland_surface, xwayland_surface->x,
			xwayland_surface->y, constrained_width, constrained_height);
}

static void move_resize(struct roots_view *view, double x, double y,
		uint32_t width, uint32_t height) {
	assert(view->type == ROOTS_XWAYLAND_VIEW);
	struct wlr_xwayland_surface *xwayland_surface = view->xwayland_surface;

	bool update_x = x != view->box.x;
	bool update_y = y != view->box.y;

	uint32_t constrained_width, constrained_height;
	apply_size_constraints(xwayland_surface, width, height, &constrained_width,
		&constrained_height);

	if (update_x) {
		x = x + width - constrained_width;
	}
	if (update_y) {
		y = y + height - constrained_height;
	}

	view->pending_move_resize.update_x = update_x;
	view->pending_move_resize.update_y = update_y;
	view->pending_move_resize.x = x;
	view->pending_move_resize.y = y;
	view->pending_move_resize.width = constrained_width;
	view->pending_move_resize.height = constrained_height;

	wlr_xwayland_surface_configure(xwayland_surface, x, y, constrained_width,
		constrained_height);
}

static void close(struct roots_view *view) {
	assert(view->type == ROOTS_XWAYLAND_VIEW);
	wlr_xwayland_surface_close(view->xwayland_surface);
}

static void maximize(struct roots_view *view, bool maximized) {
	assert(view->type == ROOTS_XWAYLAND_VIEW);

	wlr_xwayland_surface_set_maximized(view->xwayland_surface, maximized);
}

static void set_fullscreen(struct roots_view *view, bool fullscreen) {
	assert(view->type == ROOTS_XWAYLAND_VIEW);

	wlr_xwayland_surface_set_fullscreen(view->xwayland_surface, fullscreen);
}

static void destroy(struct roots_view *view) {
	assert(view->type == ROOTS_XWAYLAND_VIEW);
	struct roots_xwayland_surface *roots_surface = view->roots_xwayland_surface;
	wl_list_remove(&roots_surface->destroy.link);
	wl_list_remove(&roots_surface->request_configure.link);
	wl_list_remove(&roots_surface->request_move.link);
	wl_list_remove(&roots_surface->request_resize.link);
	wl_list_remove(&roots_surface->request_maximize.link);
	wl_list_remove(&roots_surface->map.link);
	wl_list_remove(&roots_surface->unmap.link);
	free(roots_surface);
}

static void handle_destroy(struct wl_listener *listener, void *data) {
	struct roots_xwayland_surface *roots_surface =
		wl_container_of(listener, roots_surface, destroy);
	view_destroy(roots_surface->view);
}

static void handle_request_configure(struct wl_listener *listener, void *data) {
	struct roots_xwayland_surface *roots_surface =
		wl_container_of(listener, roots_surface, request_configure);
	struct wlr_xwayland_surface *xwayland_surface =
		roots_surface->view->xwayland_surface;
	struct wlr_xwayland_surface_configure_event *event = data;

	view_update_position(roots_surface->view, event->x, event->y);

	wlr_xwayland_surface_configure(xwayland_surface, event->x, event->y,
		event->width, event->height);
}

static struct roots_seat *guess_seat_for_view(struct roots_view *view) {
	// the best we can do is to pick the first seat that has the surface focused
	// for the pointer
	struct roots_input *input = view->desktop->server->input;
	struct roots_seat *seat;
	wl_list_for_each(seat, &input->seats, link) {
		if (seat->seat->pointer_state.focused_surface == view->wlr_surface) {
			return seat;
		}
	}
	return NULL;
}

static void handle_request_move(struct wl_listener *listener, void *data) {
	struct roots_xwayland_surface *roots_surface =
		wl_container_of(listener, roots_surface, request_move);
	struct roots_view *view = roots_surface->view;
	struct roots_seat *seat = guess_seat_for_view(view);

	if (!seat || seat->cursor->mode != ROOTS_CURSOR_PASSTHROUGH) {
		return;
	}

	roots_seat_begin_move(seat, view);
}

static void handle_request_resize(struct wl_listener *listener, void *data) {
	struct roots_xwayland_surface *roots_surface =
		wl_container_of(listener, roots_surface, request_resize);
	struct roots_view *view = roots_surface->view;
	struct roots_seat *seat = guess_seat_for_view(view);
	struct wlr_xwayland_resize_event *e = data;

	if (!seat || seat->cursor->mode != ROOTS_CURSOR_PASSTHROUGH) {
		return;
	}
	roots_seat_begin_resize(seat, view, e->edges);
}

static void handle_request_maximize(struct wl_listener *listener, void *data) {
	struct roots_xwayland_surface *roots_surface =
		wl_container_of(listener, roots_surface, request_maximize);
	struct roots_view *view = roots_surface->view;
	struct wlr_xwayland_surface *xwayland_surface = view->xwayland_surface;

	bool maximized = xwayland_surface->maximized_vert &&
		xwayland_surface->maximized_horz;
	view_maximize(view, maximized);
}

static void handle_request_fullscreen(struct wl_listener *listener,
		void *data) {
	struct roots_xwayland_surface *roots_surface =
		wl_container_of(listener, roots_surface, request_fullscreen);
	struct roots_view *view = roots_surface->view;
	struct wlr_xwayland_surface *xwayland_surface = view->xwayland_surface;

	view_set_fullscreen(view, xwayland_surface->fullscreen, NULL);
}

static void handle_surface_commit(struct wl_listener *listener, void *data) {
	struct roots_xwayland_surface *roots_surface =
		wl_container_of(listener, roots_surface, surface_commit);
	struct roots_view *view = roots_surface->view;
	struct wlr_surface *wlr_surface = view->wlr_surface;

	view_apply_damage(view);

	int width = wlr_surface->current.width;
	int height = wlr_surface->current.height;
	view_update_size(view, width, height);

	double x = view->box.x;
	double y = view->box.y;
	if (view->pending_move_resize.update_x) {
		x = view->pending_move_resize.x + view->pending_move_resize.width -
			width;
		view->pending_move_resize.update_x = false;
	}
	if (view->pending_move_resize.update_y) {
		y = view->pending_move_resize.y + view->pending_move_resize.height -
			height;
		view->pending_move_resize.update_y = false;
	}
	view_update_position(view, x, y);
}

static void handle_map(struct wl_listener *listener, void *data) {
	struct roots_xwayland_surface *roots_surface =
		wl_container_of(listener, roots_surface, map);
	struct wlr_xwayland_surface *surface = data;
	struct roots_view *view = roots_surface->view;

	view->box.x = surface->x;
	view->box.y = surface->y;
	view->box.width = surface->surface->current.width;
	view->box.height = surface->surface->current.height;

	roots_surface->surface_commit.notify = handle_surface_commit;
	wl_signal_add(&surface->surface->events.commit,
		&roots_surface->surface_commit);

	view_map(view, surface->surface);

	if (!surface->override_redirect) {
		if (surface->decorations == WLR_XWAYLAND_SURFACE_DECORATIONS_ALL) {
			view->decorated = true;
			view->border_width = 4;
			view->titlebar_height = 12;
		}

		view_setup(view);
	} else {
		view_initial_focus(view);
	}
}

static void handle_unmap(struct wl_listener *listener, void *data) {
	struct roots_xwayland_surface *roots_surface =
		wl_container_of(listener, roots_surface, unmap);
	struct roots_view *view = roots_surface->view;

	wl_list_remove(&roots_surface->surface_commit.link);

	view_unmap(view);
}

void handle_xwayland_surface(struct wl_listener *listener, void *data) {
	struct roots_desktop *desktop =
		wl_container_of(listener, desktop, xwayland_surface);

	struct wlr_xwayland_surface *surface = data;
	wlr_log(WLR_DEBUG, "new xwayland surface: title=%s, class=%s, instance=%s",
		surface->title, surface->class, surface->instance);
	wlr_xwayland_surface_ping(surface);

	struct roots_xwayland_surface *roots_surface =
		calloc(1, sizeof(struct roots_xwayland_surface));
	if (roots_surface == NULL) {
		return;
	}

	roots_surface->destroy.notify = handle_destroy;
	wl_signal_add(&surface->events.destroy, &roots_surface->destroy);
	roots_surface->request_configure.notify = handle_request_configure;
	wl_signal_add(&surface->events.request_configure,
		&roots_surface->request_configure);
	roots_surface->map.notify = handle_map;
	wl_signal_add(&surface->events.map, &roots_surface->map);
	roots_surface->unmap.notify = handle_unmap;
	wl_signal_add(&surface->events.unmap, &roots_surface->unmap);
	roots_surface->request_move.notify = handle_request_move;
	wl_signal_add(&surface->events.request_move, &roots_surface->request_move);
	roots_surface->request_resize.notify = handle_request_resize;
	wl_signal_add(&surface->events.request_resize,
		&roots_surface->request_resize);
	roots_surface->request_maximize.notify = handle_request_maximize;
	wl_signal_add(&surface->events.request_maximize,
		&roots_surface->request_maximize);
	roots_surface->request_fullscreen.notify = handle_request_fullscreen;
	wl_signal_add(&surface->events.request_fullscreen,
		&roots_surface->request_fullscreen);

	struct roots_view *view = view_create(desktop);
	if (view == NULL) {
		free(roots_surface);
		return;
	}
	view->type = ROOTS_XWAYLAND_VIEW;
	view->box.x = surface->x;
	view->box.y = surface->y;

	view->xwayland_surface = surface;
	view->roots_xwayland_surface = roots_surface;
	view->activate = activate;
	view->resize = resize;
	view->move = move;
	view->move_resize = move_resize;
	view->maximize = maximize;
	view->set_fullscreen = set_fullscreen;
	view->close = close;
	view->destroy = destroy;
	roots_surface->view = view;
}
