#include <assert.h>
#include <stdlib.h>
#include <stdbool.h>
#include <wayland-server.h>
#include <wlr/types/wlr_box.h>
#include <wlr/types/wlr_surface.h>
#include <wlr/xwayland.h>
#include <wlr/util/log.h>
#include "rootston/desktop.h"
#include "rootston/server.h"

static void activate(struct roots_view *view, bool active) {
	assert(view->type == ROOTS_XWAYLAND_VIEW);
	struct wlr_xwayland *xwayland = view->desktop->xwayland;
	wlr_xwayland_surface_activate(xwayland, view->xwayland_surface, active);
}

static void resize(struct roots_view *view, uint32_t width, uint32_t height) {
	assert(view->type == ROOTS_XWAYLAND_VIEW);
	struct wlr_xwayland_surface *xwayland_surface = view->xwayland_surface;
	wlr_xwayland_surface_configure(view->desktop->xwayland, xwayland_surface,
		xwayland_surface->x, xwayland_surface->y, width, height);
}

static void set_position(struct roots_view *view, double x, double y) {
	assert(view->type == ROOTS_XWAYLAND_VIEW);
	struct wlr_xwayland_surface *xwayland_surface = view->xwayland_surface;
	view->x = x;
	view->y = y;
	wlr_xwayland_surface_configure(view->desktop->xwayland, xwayland_surface,
		x, y, xwayland_surface->width, xwayland_surface->height);
}

static void close(struct roots_view *view) {
	assert(view->type == ROOTS_XWAYLAND_VIEW);
	wlr_xwayland_surface_close(view->desktop->xwayland, view->xwayland_surface);
}

static void handle_destroy(struct wl_listener *listener, void *data) {
	struct roots_xwayland_surface *roots_surface =
		wl_container_of(listener, roots_surface, destroy);
	wl_list_remove(&roots_surface->destroy.link);
	wl_list_remove(&roots_surface->map_notify.link);
	wl_list_remove(&roots_surface->unmap_notify.link);
	view_destroy(roots_surface->view);
	free(roots_surface);
}

static void handle_request_configure(struct wl_listener *listener, void *data) {
	struct roots_xwayland_surface *roots_surface =
		wl_container_of(listener, roots_surface, request_configure);
	struct wlr_xwayland_surface *xwayland_surface =
		roots_surface->view->xwayland_surface;
	struct wlr_xwayland_surface_configure_event *event = data;

	roots_surface->view->x = (double)event->x;
	roots_surface->view->y = (double)event->y;

	wlr_xwayland_surface_configure(roots_surface->view->desktop->xwayland,
		xwayland_surface, event->x, event->y, event->width, event->height);
}

// XXX Needs deep refactoring to get this better. We need to select the correct
// seat based on seat pointer focus, but interactive moving and resizing is not
// yet seat aware. Even then, we can only guess because X11 events don't give us
// enough wayland info to know for sure.
static struct wlr_cursor *guess_cursor_for_view(struct roots_view *view) {
	struct roots_input *input = view->desktop->server->input;
	size_t len = sizeof(input->input_events) / sizeof(*input->input_events);
	for (size_t i = 0; i < len; i++) {
		struct wlr_cursor *cursor = input->input_events[i].cursor;
		if (cursor) {
			int width = view->xwayland_surface->surface->current->width;
			int height = view->xwayland_surface->surface->current->height;
			if (cursor->x > view->x && cursor->y > view->y &&
					cursor->x < view->x + width &&
					cursor->y < view->y + height) {
				return cursor;
			}
		}
	}

	return NULL;
}

static void handle_request_move(struct wl_listener *listener, void *data) {
	struct roots_xwayland_surface *roots_surface =
		wl_container_of(listener, roots_surface, request_move);
	struct roots_view *view = roots_surface->view;
	struct roots_input *input = view->desktop->server->input;
	struct wlr_cursor *cursor = guess_cursor_for_view(view);

	if (!cursor || input->mode != ROOTS_CURSOR_PASSTHROUGH) {
		return;
	}

	view_begin_move(input, cursor, view);
}

static void handle_request_resize(struct wl_listener *listener, void *data) {
	struct roots_xwayland_surface *roots_surface =
		wl_container_of(listener, roots_surface, request_resize);
	struct roots_view *view = roots_surface->view;
	struct roots_input *input = view->desktop->server->input;
	struct wlr_cursor *cursor = guess_cursor_for_view(view);
	struct wlr_xwayland_resize_event *e = data;

	if (!cursor || input->mode != ROOTS_CURSOR_PASSTHROUGH) {
		return;
	}
	view_begin_resize(input, cursor, view, e->edges);
}

static void handle_map_notify(struct wl_listener *listener, void *data) {
	struct roots_xwayland_surface *roots_surface =
		wl_container_of(listener, roots_surface, map_notify);
	struct roots_view *view = roots_surface->view;
	struct wlr_xwayland_surface *xsurface = view->xwayland_surface;
	struct roots_desktop *desktop = view->desktop;

	view->wlr_surface = xsurface->surface;
	view->x = (double)xsurface->x;
	view->y = (double)xsurface->y;

	wlr_list_push(desktop->views, roots_surface->view);
}

static void handle_unmap_notify(struct wl_listener *listener, void *data) {
	struct roots_xwayland_surface *roots_surface =
		wl_container_of(listener, roots_surface, unmap_notify);
	struct roots_desktop *desktop = roots_surface->view->desktop;
	roots_surface->view->wlr_surface = NULL;

	for (size_t i = 0; i < desktop->views->length; i++) {
		if (desktop->views->items[i] == roots_surface->view) {
			wlr_list_del(desktop->views, i);
			break;
		}
	}
}

void handle_xwayland_surface(struct wl_listener *listener, void *data) {
	struct roots_desktop *desktop =
		wl_container_of(listener, desktop, xwayland_surface);

	struct wlr_xwayland_surface *surface = data;
	wlr_log(L_DEBUG, "new xwayland surface: title=%s, class=%s, instance=%s",
		surface->title, surface->class, surface->instance);

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

	roots_surface->map_notify.notify = handle_map_notify;
	wl_signal_add(&surface->events.map_notify, &roots_surface->map_notify);

	roots_surface->unmap_notify.notify = handle_unmap_notify;
	wl_signal_add(&surface->events.unmap_notify, &roots_surface->unmap_notify);

	roots_surface->request_move.notify = handle_request_move;
	wl_signal_add(&surface->events.request_move, &roots_surface->request_move);

	roots_surface->request_resize.notify = handle_request_resize;
	wl_signal_add(&surface->events.request_resize, &roots_surface->request_resize);

	struct roots_view *view = calloc(1, sizeof(struct roots_view));
	if (view == NULL) {
		free(roots_surface);
		return;
	}
	view->type = ROOTS_XWAYLAND_VIEW;
	view->x = (double)surface->x;
	view->y = (double)surface->y;
	view->xwayland_surface = surface;
	view->roots_xwayland_surface = roots_surface;
	view->wlr_surface = surface->surface;
	view->desktop = desktop;
	view->activate = activate;
	view->resize = resize;
	view->set_position = set_position;
	view->close = close;
	roots_surface->view = view;
	wlr_list_add(desktop->views, view);

	if (!surface->override_redirect) {
		view_initialize(view);
	}
}
