#include <assert.h>
#include <stdlib.h>
#include <stdbool.h>
#include <wayland-server.h>
#include <wlr/types/wlr_box.h>
#include <wlr/types/wlr_surface.h>
#include <wlr/types/wlr_wl_shell.h>
#include <wlr/util/log.h>
#include "rootston/desktop.h"
#include "rootston/server.h"
#include "rootston/input.h"

static void resize(struct roots_view *view, uint32_t width, uint32_t height) {
	assert(view->type == ROOTS_WL_SHELL_VIEW);
	struct wlr_wl_shell_surface *surf = view->wl_shell_surface;
	wlr_wl_shell_surface_configure(surf, WL_SHELL_SURFACE_RESIZE_NONE, width,
		height);
}

static void close(struct roots_view *view) {
	assert(view->type == ROOTS_WL_SHELL_VIEW);
	struct wlr_wl_shell_surface *surf = view->wl_shell_surface;
	wl_client_destroy(surf->client);
}

static void handle_request_move(struct wl_listener *listener, void *data) {
	struct roots_wl_shell_surface *roots_surface =
		wl_container_of(listener, roots_surface, request_move);
	struct roots_view *view = roots_surface->view;
	struct roots_input *input = view->desktop->server->input;
	struct wlr_wl_shell_surface_move_event *e = data;
	struct roots_seat *seat = input_seat_from_wlr_seat(input, e->seat->seat);
	if (!seat || seat->cursor->mode != ROOTS_CURSOR_PASSTHROUGH) {
		return;
	}
	roots_seat_begin_move(seat, view);
}

static void handle_request_resize(struct wl_listener *listener, void *data) {
	struct roots_wl_shell_surface *roots_surface =
		wl_container_of(listener, roots_surface, request_resize);
	struct roots_view *view = roots_surface->view;
	struct roots_input *input = view->desktop->server->input;
	struct wlr_wl_shell_surface_resize_event *e = data;
	struct roots_seat *seat = input_seat_from_wlr_seat(input, e->seat->seat);
	if (!seat || seat->cursor->mode != ROOTS_CURSOR_PASSTHROUGH) {
		return;
	}
	roots_seat_begin_resize(seat, view, e->edges);
}

static void handle_request_maximize(struct wl_listener *listener,
		void *data) {
	struct roots_wl_shell_surface *roots_surface =
		wl_container_of(listener, roots_surface, request_maximize);
	struct roots_view *view = roots_surface->view;
	//struct wlr_wl_shell_surface_maximize_event *e = data;
	view_maximize(view, true);
}

static void handle_request_fullscreen(struct wl_listener *listener,
		void *data) {
	struct roots_wl_shell_surface *roots_surface =
		wl_container_of(listener, roots_surface, request_fullscreen);
	struct roots_view *view = roots_surface->view;
	struct wlr_wl_shell_surface_set_fullscreen_event *e = data;
	view_set_fullscreen(view, true, e->output);
}

static void handle_set_state(struct wl_listener *listener, void *data) {
	struct roots_wl_shell_surface *roots_surface =
		wl_container_of(listener, roots_surface, set_state);
	struct roots_view *view = roots_surface->view;
	struct wlr_wl_shell_surface *surface = view->wl_shell_surface;
	if (view->maximized &&
			surface->state != WLR_WL_SHELL_SURFACE_STATE_MAXIMIZED) {
		view_maximize(view, false);
	}
	if (view->fullscreen_output != NULL &&
			surface->state != WLR_WL_SHELL_SURFACE_STATE_FULLSCREEN) {
		view_set_fullscreen(view, false, NULL);
	}
}

static void handle_surface_commit(struct wl_listener *listener, void *data) {
	struct roots_wl_shell_surface *roots_surface =
		wl_container_of(listener, roots_surface, surface_commit);
	struct roots_view *view = roots_surface->view;
	struct wlr_surface *wlr_surface = view->wlr_surface;

	int width = wlr_surface->current->width;
	int height = wlr_surface->current->height;

	if (view->pending_move_resize.update_x) {
		view->x = view->pending_move_resize.x +
			view->pending_move_resize.width - width;
		view->pending_move_resize.update_x = false;
	}
	if (view->pending_move_resize.update_y) {
		view->y = view->pending_move_resize.y +
			view->pending_move_resize.height - height;
		view->pending_move_resize.update_y = false;
	}
}

static void handle_destroy(struct wl_listener *listener, void *data) {
	struct roots_wl_shell_surface *roots_surface =
		wl_container_of(listener, roots_surface, destroy);
	wl_list_remove(&roots_surface->destroy.link);
	wl_list_remove(&roots_surface->request_move.link);
	wl_list_remove(&roots_surface->request_resize.link);
	wl_list_remove(&roots_surface->request_maximize.link);
	wl_list_remove(&roots_surface->request_fullscreen.link);
	wl_list_remove(&roots_surface->set_state.link);
	wl_list_remove(&roots_surface->surface_commit.link);
	wl_list_remove(&roots_surface->view->link);
	view_destroy(roots_surface->view);
	free(roots_surface);
}

void handle_wl_shell_surface(struct wl_listener *listener, void *data) {
	struct roots_desktop *desktop =
		wl_container_of(listener, desktop, wl_shell_surface);

	struct wlr_wl_shell_surface *surface = data;
	wlr_log(L_DEBUG, "new shell surface: title=%s, class=%s",
		surface->title, surface->class);
	wlr_wl_shell_surface_ping(surface);

	struct roots_wl_shell_surface *roots_surface =
		calloc(1, sizeof(struct roots_wl_shell_surface));
	if (!roots_surface) {
		return;
	}
	roots_surface->destroy.notify = handle_destroy;
	wl_signal_add(&surface->events.destroy, &roots_surface->destroy);
	roots_surface->request_move.notify = handle_request_move;
	wl_signal_add(&surface->events.request_move, &roots_surface->request_move);
	roots_surface->request_resize.notify = handle_request_resize;
	wl_signal_add(&surface->events.request_resize,
		&roots_surface->request_resize);
	roots_surface->request_maximize.notify = handle_request_maximize;
	wl_signal_add(&surface->events.request_maximize,
		&roots_surface->request_maximize);
	roots_surface->request_fullscreen.notify =
		handle_request_fullscreen;
	wl_signal_add(&surface->events.request_fullscreen,
		&roots_surface->request_fullscreen);
	roots_surface->set_state.notify = handle_set_state;
	wl_signal_add(&surface->events.set_state, &roots_surface->set_state);
	roots_surface->surface_commit.notify = handle_surface_commit;
	wl_signal_add(&surface->surface->events.commit, &roots_surface->surface_commit);

	struct roots_view *view = calloc(1, sizeof(struct roots_view));
	if (!view) {
		free(roots_surface);
		return;
	}
	view->type = ROOTS_WL_SHELL_VIEW;

	view->wl_shell_surface = surface;
	view->roots_wl_shell_surface = roots_surface;
	view->wlr_surface = surface->surface;
	view->resize = resize;
	view->close = close;
	roots_surface->view = view;
	view_init(view, desktop);
	wl_list_insert(&desktop->views, &view->link);

	view_setup(view);

	if (surface->state == WLR_WL_SHELL_SURFACE_STATE_TRANSIENT) {
		// We need to map it relative to the parent
		bool found = false;
		struct roots_view *parent;
		wl_list_for_each(parent, &desktop->views, link) {
			if (parent->type == ROOTS_WL_SHELL_VIEW &&
					parent->wl_shell_surface == surface->parent) {
				found = true;
				break;
			}
		}
		if (found) {
			view_move(view,
				parent->x + surface->transient_state->x,
				parent->y + surface->transient_state->y);
		}
	}
}
