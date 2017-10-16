#include <assert.h>
#include <stdlib.h>
#include <stdbool.h>
#include <wayland-server.h>
#include <wlr/types/wlr_box.h>
#include <wlr/types/wlr_surface.h>
#include <wlr/types/wlr_xdg_shell_v6.h>
#include <wlr/util/log.h>
#include "rootston/desktop.h"
#include "rootston/server.h"
#include "rootston/input.h"

static void get_size(struct roots_view *view, struct wlr_box *box) {
	assert(view->type == ROOTS_XDG_SHELL_V6_VIEW);
	struct wlr_xdg_surface_v6 *surf = view->xdg_surface_v6;
	// TODO: surf->geometry can be NULL
	memcpy(box, surf->geometry, sizeof(struct wlr_box));
}

static void activate(struct roots_view *view, bool active) {
	assert(view->type == ROOTS_XDG_SHELL_V6_VIEW);
	struct wlr_xdg_surface_v6 *surf = view->xdg_surface_v6;
	if (surf->role == WLR_XDG_SURFACE_V6_ROLE_TOPLEVEL) {
		wlr_xdg_toplevel_v6_set_activated(surf, active);
	}
}

static void resize(struct roots_view *view, uint32_t width, uint32_t height) {
	assert(view->type == ROOTS_XDG_SHELL_V6_VIEW);
	struct wlr_xdg_surface_v6 *surf = view->xdg_surface_v6;
	if (surf->role == WLR_XDG_SURFACE_V6_ROLE_TOPLEVEL) {
		wlr_xdg_toplevel_v6_set_size(surf, width, height);
	}
}

static void close(struct roots_view *view) {
	assert(view->type == ROOTS_XDG_SHELL_V6_VIEW);
	struct wlr_xdg_surface_v6 *surf = view->xdg_surface_v6;
	if (surf->role == WLR_XDG_SURFACE_V6_ROLE_TOPLEVEL) {
		wlr_xdg_toplevel_v6_send_close(surf);
	}
}

static void handle_request_move(struct wl_listener *listener, void *data) {
	struct roots_xdg_surface_v6 *roots_xdg_surface =
		wl_container_of(listener, roots_xdg_surface, request_move);
	struct roots_view *view = roots_xdg_surface->view;
	struct roots_input *input = view->desktop->server->input;
	struct wlr_xdg_toplevel_v6_move_event *e = data;
	const struct roots_input_event *event = get_input_event(input, e->serial);
	if (!event || input->mode != ROOTS_CURSOR_PASSTHROUGH) {
		return;
	}
	view_begin_move(input, event->cursor, view);
}

static void handle_request_resize(struct wl_listener *listener, void *data) {
	struct roots_xdg_surface_v6 *roots_xdg_surface =
		wl_container_of(listener, roots_xdg_surface, request_resize);
	struct roots_view *view = roots_xdg_surface->view;
	struct roots_input *input = view->desktop->server->input;
	struct wlr_xdg_toplevel_v6_resize_event *e = data;
	const struct roots_input_event *event = get_input_event(input, e->serial);
	if (!event || input->mode != ROOTS_CURSOR_PASSTHROUGH) {
		return;
	}
	view_begin_resize(input, event->cursor, view, e->edges);
}

static void handle_commit(struct wl_listener *listener, void *data) {
	// TODO is there anything we need to do here?
}

static void handle_destroy(struct wl_listener *listener, void *data) {
	struct roots_xdg_surface_v6 *roots_xdg_surface =
		wl_container_of(listener, roots_xdg_surface, destroy);
	wl_list_remove(&roots_xdg_surface->destroy.link);
	wl_list_remove(&roots_xdg_surface->ping_timeout.link);
	wl_list_remove(&roots_xdg_surface->request_move.link);
	wl_list_remove(&roots_xdg_surface->request_resize.link);
	wl_list_remove(&roots_xdg_surface->request_show_window_menu.link);
	wl_list_remove(&roots_xdg_surface->request_minimize.link);
	view_destroy(roots_xdg_surface->view);
	free(roots_xdg_surface);
}

void handle_xdg_shell_v6_surface(struct wl_listener *listener, void *data) {
	struct wlr_xdg_surface_v6 *surface = data;
	assert(surface->role != WLR_XDG_SURFACE_V6_ROLE_NONE);

	if (surface->role == WLR_XDG_SURFACE_V6_ROLE_POPUP) {
		wlr_log(L_DEBUG, "new xdg popup");
		return;
	}

	struct roots_desktop *desktop =
		wl_container_of(listener, desktop, xdg_shell_v6_surface);

	wlr_log(L_DEBUG, "new xdg toplevel: title=%s, app_id=%s",
		surface->title, surface->app_id);
	wlr_xdg_surface_v6_ping(surface);

	struct roots_xdg_surface_v6 *roots_surface =
		calloc(1, sizeof(struct roots_xdg_surface_v6));
	if (!roots_surface) {
		return;
	}
	wl_list_init(&roots_surface->commit.link);
	roots_surface->commit.notify = handle_commit;
	wl_signal_add(&surface->events.commit, &roots_surface->commit);
	wl_list_init(&roots_surface->destroy.link);
	roots_surface->destroy.notify = handle_destroy;
	wl_signal_add(&surface->events.destroy, &roots_surface->destroy);
	wl_list_init(&roots_surface->ping_timeout.link);
	wl_list_init(&roots_surface->request_minimize.link);
	wl_list_init(&roots_surface->request_move.link);
	roots_surface->request_move.notify = handle_request_move;
	wl_signal_add(&surface->events.request_move, &roots_surface->request_move);
	wl_list_init(&roots_surface->request_resize.link);
	roots_surface->request_resize.notify = handle_request_resize;
	wl_signal_add(&surface->events.request_resize,
		&roots_surface->request_resize);
	wl_list_init(&roots_surface->request_show_window_menu.link);

	struct roots_view *view = calloc(1, sizeof(struct roots_view));
	view->type = ROOTS_XDG_SHELL_V6_VIEW;
	view->xdg_surface_v6 = surface;
	view->roots_xdg_surface_v6 = roots_surface;
	view->wlr_surface = surface->surface;
	view->get_size = get_size;
	view->activate = activate;
	view->resize = resize;
	view->close = close;
	view->desktop = desktop;
	roots_surface->view = view;
	list_add(desktop->views, view);

	view_initialize(view);
}
