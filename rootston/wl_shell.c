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
	const struct roots_input_event *event = get_input_event(input, e->serial);
	if (!event || input->mode != ROOTS_CURSOR_PASSTHROUGH) {
		return;
	}
	view_begin_move(input, event->cursor, view);
}

static void handle_request_resize(struct wl_listener *listener, void *data) {
	struct roots_wl_shell_surface *roots_surface =
		wl_container_of(listener, roots_surface, request_resize);
	struct roots_view *view = roots_surface->view;
	struct roots_input *input = view->desktop->server->input;
	struct wlr_wl_shell_surface_resize_event *e = data;
	const struct roots_input_event *event = get_input_event(input, e->serial);
	if (!event || input->mode != ROOTS_CURSOR_PASSTHROUGH) {
		return;
	}
	view_begin_resize(input, event->cursor, view, e->edges);
}

static void handle_surface_commit(struct wl_listener *listener, void *data) {
	struct roots_wl_shell_surface *roots_surface =
		wl_container_of(listener, roots_surface, surface_commit);
	struct roots_view *view = roots_surface->view;

	if (view->wl_shell_surface->state == WLR_WL_SHELL_SURFACE_STATE_TOPLEVEL &&
			!roots_surface->initialized) {
		roots_surface->initialized = view_initialize(view);
	}
}

static void handle_destroy(struct wl_listener *listener, void *data) {
	struct roots_wl_shell_surface *roots_surface =
		wl_container_of(listener, roots_surface, destroy);
	wl_list_remove(&roots_surface->destroy.link);
	wl_list_remove(&roots_surface->ping_timeout.link);
	wl_list_remove(&roots_surface->request_move.link);
	wl_list_remove(&roots_surface->request_resize.link);
	wl_list_remove(&roots_surface->request_set_fullscreen.link);
	wl_list_remove(&roots_surface->request_set_maximized.link);
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
	wl_list_init(&roots_surface->destroy.link);
	roots_surface->destroy.notify = handle_destroy;
	wl_signal_add(&surface->events.destroy, &roots_surface->destroy);
	wl_list_init(&roots_surface->ping_timeout.link);
	wl_list_init(&roots_surface->request_move.link);
	roots_surface->request_move.notify = handle_request_move;
	wl_signal_add(&surface->events.request_move, &roots_surface->request_move);
	wl_list_init(&roots_surface->request_resize.link);
	roots_surface->request_resize.notify = handle_request_resize;
	wl_signal_add(&surface->events.request_resize,
		&roots_surface->request_resize);
	wl_list_init(&roots_surface->request_set_fullscreen.link);
	wl_list_init(&roots_surface->request_set_maximized.link);
	wl_list_init(&roots_surface->surface_commit.link);
	roots_surface->surface_commit.notify = handle_surface_commit;
	wl_signal_add(&surface->surface->events.commit,
		&roots_surface->surface_commit);

	struct roots_view *view = calloc(1, sizeof(struct roots_view));
	view->type = ROOTS_WL_SHELL_VIEW;

	if (surface->state == WLR_WL_SHELL_SURFACE_STATE_TRANSIENT) {
		// we need to map it relative to the parent
		int i =
			list_seq_find(desktop->views,
				shell_surface_compare_equals, surface->parent);
		if (i != -1) {
			struct roots_view *parent = desktop->views->items[i];
			view->x = parent->x + surface->transient_state->x;
			view->y = parent->y + surface->transient_state->y;
		}
	} else {
		view->x = view->y = 200;
	}
	view->wl_shell_surface = surface;
	view->roots_wl_shell_surface = roots_surface;
	view->wlr_surface = surface->surface;
	view->resize = resize;
	view->close = close;
	view->desktop = desktop;
	roots_surface->view = view;
	list_add(desktop->views, view);
}

struct wlr_wl_shell_surface *wlr_wl_shell_surface_popup_at(
		struct wlr_wl_shell_surface *surface, double sx, double sy,
		double *popup_sx, double *popup_sy) {
	struct wlr_wl_shell_surface *popup;
	wl_list_for_each(popup, &surface->children, child_link) {
		double _popup_sx = popup->transient_state->x;
		double _popup_sy = popup->transient_state->y;
		int popup_width =
			popup->surface->current->buffer_width;
		int popup_height =
			popup->surface->current->buffer_height;

		struct wlr_wl_shell_surface *_popup =
			wlr_wl_shell_surface_popup_at(popup,
				popup->transient_state->x,
				popup->transient_state->y,
				popup_sx, popup_sy);
		if (_popup) {
			*popup_sx = sx + _popup_sx;
			*popup_sy = sy + _popup_sy;
			return _popup;
		}

		if ((sx > _popup_sx && sx < _popup_sx + popup_width) &&
				(sy > _popup_sy && sy < _popup_sy + popup_height)) {
			if (pixman_region32_contains_point(&popup->surface->current->input,
						sx - _popup_sx, sy - _popup_sy, NULL)) {
				*popup_sx = _popup_sx;
				*popup_sy = _popup_sy;
				return popup;
			}
		}
	}

	return NULL;
}
