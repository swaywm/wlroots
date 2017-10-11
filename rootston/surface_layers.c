#include <assert.h>
#include <stdlib.h>
#include <stdbool.h>
#include <wayland-server.h>
#include <wlr/types/wlr_box.h>
#include <wlr/types/wlr_surface.h>
#include <wlr/types/wlr_surface_layers.h>
#include <wlr/util/log.h>
#include "rootston/desktop.h"
#include "rootston/server.h"
#include "rootston/input.h"

static void handle_destroy(struct wl_listener *listener, void *data) {
	struct roots_layer_surface *roots_surface =
		wl_container_of(listener, roots_surface, destroy);
	wl_list_remove(&roots_surface->destroy.link);
	view_destroy(roots_surface->view);
	free(roots_surface);
}

void handle_surface_layers_surface(struct wl_listener *listener, void *data) {
	struct roots_desktop *desktop =
		wl_container_of(listener, desktop, surface_layers_surface);

	struct wlr_layer_surface *surface = data;
	wlr_log(L_DEBUG, "new surface_layers surface");

	struct roots_layer_surface *roots_surface =
		calloc(1, sizeof(struct roots_layer_surface));
	if (!roots_surface) {
		return;
	}
	roots_surface->destroy.notify = handle_destroy;
	wl_list_init(&roots_surface->destroy.link);
	wl_signal_add(&surface->events.destroy, &roots_surface->destroy);

	struct roots_view *view = calloc(1, sizeof(struct roots_view));
	if (!view) {
		free(roots_surface);
		return;
	}
	view->type = ROOTS_SURFACE_LAYERS_VIEW;

	view->layer_surface = surface;
	view->roots_layer_surface = roots_surface;
	view->wlr_surface = surface->surface;
	view->desktop = desktop;
	roots_surface->view = view;
	list_add(desktop->views, view);
}
