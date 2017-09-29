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

static void handle_destroy(struct wl_listener *listener, void *data) {
	struct roots_xwayland_surface *roots_surface =
		wl_container_of(listener, roots_surface, destroy);
	wl_list_remove(&roots_surface->destroy.link);
	view_destroy(roots_surface->view);
	free(roots_surface);
}

static void handle_configure(struct wl_listener *listener, void *data) {
	struct roots_xwayland_surface *roots_surface =
		wl_container_of(listener, roots_surface, destroy);
	struct wlr_xwayland_surface_configure_event *event = data;
	struct wlr_xwayland_surface *xwayland_surface = event->surface;

	xwayland_surface->x = event->x;
	xwayland_surface->y = event->y;
	xwayland_surface->width = event->width;
	xwayland_surface->height = event->height;

	roots_surface->view->x = (double)event->x;
	roots_surface->view->y = (double)event->y;

	wlr_xwayland_surface_configure(roots_surface->view->desktop->xwayland,
		xwayland_surface);
}

static void activate(struct roots_view *view, bool active) {
	wlr_xwayland_surface_activate(view->desktop->xwayland,
		view->xwayland_surface);
}

void handle_xwayland_surface(struct wl_listener *listener, void *data) {
	struct roots_desktop *desktop =
		wl_container_of(listener, desktop, xwayland_surface);

	struct wlr_xwayland_surface *surface = data;
	// TODO: get and log title, class, etc
	wlr_log(L_DEBUG, "new xwayland surface");

	struct roots_xwayland_surface *roots_surface =
		calloc(1, sizeof(struct roots_xwayland_surface));
	wl_list_init(&roots_surface->destroy.link);
	roots_surface->destroy.notify = handle_destroy;
	wl_signal_add(&surface->events.destroy, &roots_surface->destroy);
	wl_list_init(&roots_surface->request_configure.link);
	roots_surface->request_configure.notify = handle_configure;
	wl_signal_add(&surface->events.request_configure,
		&roots_surface->request_configure);

	struct roots_view *view = calloc(1, sizeof(struct roots_view));
	view->type = ROOTS_XWAYLAND_VIEW;
	view->x = (double)surface->x;
	view->y = (double)surface->y;
	view->xwayland_surface = surface;
	view->roots_xwayland_surface = roots_surface;
	view->wlr_surface = surface->surface;
	view->desktop = desktop;
	view->activate = activate;
	roots_surface->view = view;
	list_add(desktop->views, view);
}
