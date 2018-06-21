#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <wayland-server.h>
#include <wlr/types/wlr_box.h>
#include <wlr/types/wlr_surface.h>
#include <wlr/types/wlr_wl_shell.h>
#include <wlr/util/log.h>
#include "rootston/desktop.h"
#include "rootston/input.h"
#include "rootston/server.h"

static void popup_destroy(struct roots_view_child *child) {
	assert(child->destroy == popup_destroy);
	struct roots_wl_shell_popup *popup = (struct roots_wl_shell_popup *)child;
	if (popup == NULL) {
		return;
	}
	wl_list_remove(&popup->destroy.link);
	wl_list_remove(&popup->set_state.link);
	wl_list_remove(&popup->new_popup.link);
	view_child_finish(&popup->view_child);
	free(popup);
}

static void popup_handle_destroy(struct wl_listener *listener, void *data) {
	struct roots_wl_shell_popup *popup =
		wl_container_of(listener, popup, destroy);
	popup_destroy((struct roots_view_child *)popup);
}

static void popup_handle_set_state(struct wl_listener *listener, void *data) {
	struct roots_wl_shell_popup *popup =
		wl_container_of(listener, popup, set_state);
	popup_destroy((struct roots_view_child *)popup);
}

static struct roots_wl_shell_popup *popup_create(struct roots_view *view,
	struct wlr_wl_shell_surface *wlr_wl_shell_surface);

static void popup_handle_new_popup(struct wl_listener *listener, void *data) {
	struct roots_wl_shell_popup *popup =
		wl_container_of(listener, popup, new_popup);
	struct wlr_wl_shell_surface *wlr_wl_shell_surface = data;
	popup_create(popup->view_child.view, wlr_wl_shell_surface);
}

static struct roots_wl_shell_popup *popup_create(struct roots_view *view,
		struct wlr_wl_shell_surface *wlr_wl_shell_surface) {
	struct roots_wl_shell_popup *popup =
		calloc(1, sizeof(struct roots_wl_shell_popup));
	if (popup == NULL) {
		return NULL;
	}
	popup->wlr_wl_shell_surface = wlr_wl_shell_surface;
	popup->view_child.destroy = popup_destroy;
	view_child_init(&popup->view_child, view, wlr_wl_shell_surface->surface);
	popup->destroy.notify = popup_handle_destroy;
	wl_signal_add(&wlr_wl_shell_surface->events.destroy, &popup->destroy);
	popup->set_state.notify = popup_handle_set_state;
	wl_signal_add(&wlr_wl_shell_surface->events.set_state, &popup->set_state);
	popup->new_popup.notify = popup_handle_new_popup;
	wl_signal_add(&wlr_wl_shell_surface->events.new_popup, &popup->new_popup);
	return popup;
}


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

static void destroy(struct roots_view *view) {
	assert(view->type == ROOTS_WL_SHELL_VIEW);
	struct roots_wl_shell_surface *roots_surface = view->roots_wl_shell_surface;
	wl_list_remove(&roots_surface->destroy.link);
	wl_list_remove(&roots_surface->request_move.link);
	wl_list_remove(&roots_surface->request_resize.link);
	wl_list_remove(&roots_surface->request_maximize.link);
	wl_list_remove(&roots_surface->request_fullscreen.link);
	wl_list_remove(&roots_surface->set_state.link);
	wl_list_remove(&roots_surface->surface_commit.link);
	free(roots_surface);
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

	view_apply_damage(view);

	int width = wlr_surface->current.width;
	int height = wlr_surface->current.height;
	view_update_size(view, width, height);

	double x = view->x;
	double y = view->y;
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

static void handle_new_popup(struct wl_listener *listener, void *data) {
	struct roots_wl_shell_surface *roots_surface =
		wl_container_of(listener, roots_surface, new_popup);
	struct wlr_wl_shell_surface *wlr_wl_shell_surface = data;
	popup_create(roots_surface->view, wlr_wl_shell_surface);
}

static void handle_destroy(struct wl_listener *listener, void *data) {
	struct roots_wl_shell_surface *roots_surface =
		wl_container_of(listener, roots_surface, destroy);
	view_destroy(roots_surface->view);
}

void handle_wl_shell_surface(struct wl_listener *listener, void *data) {
	struct roots_desktop *desktop =
		wl_container_of(listener, desktop, wl_shell_surface);
	struct wlr_wl_shell_surface *surface = data;

	if (surface->state == WLR_WL_SHELL_SURFACE_STATE_POPUP) {
		wlr_log(L_DEBUG, "new wl shell popup");
		return;
	}

	wlr_log(L_DEBUG, "new wl shell surface: title=%s, class=%s",
		surface->title, surface->class);
	wlr_wl_shell_surface_ping(surface);

	struct roots_wl_shell_surface *roots_surface =
		calloc(1, sizeof(struct roots_wl_shell_surface));
	if (!roots_surface) {
		return;
	}
	roots_surface->destroy.notify = handle_destroy;
	wl_signal_add(&surface->events.destroy, &roots_surface->destroy);
	roots_surface->new_popup.notify = handle_new_popup;
	wl_signal_add(&surface->events.new_popup, &roots_surface->new_popup);
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

	struct roots_view *view = view_create(desktop);
	if (!view) {
		free(roots_surface);
		return;
	}
	view->type = ROOTS_WL_SHELL_VIEW;
	view->width = surface->surface->current.width;
	view->height = surface->surface->current.height;

	view->wl_shell_surface = surface;
	view->roots_wl_shell_surface = roots_surface;
	view->resize = resize;
	view->close = close;
	view->destroy = destroy;
	roots_surface->view = view;

	view_map(view, surface->surface);
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
