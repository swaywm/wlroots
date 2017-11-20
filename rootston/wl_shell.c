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
	// TODO verify input event
	if (!seat || seat->cursor->mode != ROOTS_CURSOR_PASSTHROUGH) {
		return;
	}
	roots_seat_begin_resize(seat, view, e->edges);
}

static void handle_request_set_maximized(struct wl_listener *listener,
		void *data) {
	struct roots_wl_shell_surface *roots_surface =
		wl_container_of(listener, roots_surface, request_set_maximized);
	struct roots_view *view = roots_surface->view;
	//struct wlr_wl_shell_surface_set_maximized_event *e = data;
	view_maximize(view, true);
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
	view_teardown(roots_surface->view);
	wl_list_remove(&roots_surface->destroy.link);
	wl_list_remove(&roots_surface->request_move.link);
	wl_list_remove(&roots_surface->request_resize.link);
	wl_list_remove(&roots_surface->request_set_maximized.link);
	wl_list_remove(&roots_surface->set_state.link);
	wl_list_remove(&roots_surface->surface_commit.link);
	view_destroy(roots_surface->view);
	free(roots_surface);
}

static int shell_surface_compare_equals(const void *item, const void *cmp_to) {
	const struct roots_view *view = item;
	if (view->type == ROOTS_WL_SHELL_VIEW && view->wl_shell_surface == cmp_to) {
		return 0;
	}
	return -1;
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
	roots_surface->request_set_maximized.notify = handle_request_set_maximized;
	wl_signal_add(&surface->events.request_set_maximized,
		&roots_surface->request_set_maximized);
	roots_surface->set_state.notify = handle_set_state;
	wl_signal_add(&surface->events.set_state, &roots_surface->set_state);
	roots_surface->surface_commit.notify = handle_surface_commit;
	wl_signal_add(&surface->surface->events.commit,
		&roots_surface->surface_commit);

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
	view->desktop = desktop;
	roots_surface->view = view;
	wlr_list_add(desktop->views, view);
	view_setup(view);

	if (surface->state == WLR_WL_SHELL_SURFACE_STATE_TRANSIENT) {
		// we need to map it relative to the parent
		int i = wlr_list_seq_find(desktop->views, shell_surface_compare_equals,
			surface->parent);
		if (i != -1) {
			struct roots_view *parent = desktop->views->items[i];
			view_move(view,
				parent->x + surface->transient_state->x,
				parent->y + surface->transient_state->y);
		}
	}
}
