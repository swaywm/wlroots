#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <wayland-server.h>
#include <wlr/types/wlr_box.h>
#include <wlr/types/wlr_surface.h>
#include <wlr/types/wlr_layer_shell.h>
#include <wlr/util/log.h>
#include "rootston/desktop.h"
#include "rootston/layers.h"
#include "rootston/server.h"

static void handle_destroy(struct wl_listener *listener, void *data) {
	// TODO
}

static void handle_surface_commit(struct wl_listener *listener, void *data) {
	// TODO
}

static void handle_map(struct wl_listener *listener, void *data) {
	// TODO
}

static void handle_unmap(struct wl_listener *listener, void *data) {
	// TODO
}

void handle_layer_shell_surface(struct wl_listener *listener, void *data) {
	struct wlr_layer_surface *layer_surface = data;
	struct roots_desktop *desktop =
		wl_container_of(listener, desktop, layer_shell_surface);
	wlr_log(L_DEBUG, "new layer surface: namespace %s layer %d",
		layer_surface->namespace, layer_surface->layer);

	struct roots_layer_surface *roots_surface =
		calloc(1, sizeof(struct roots_layer_surface));
	if (!roots_surface) {
		return;
	}
	roots_surface->surface_commit.notify = handle_surface_commit;
	wl_signal_add(&layer_surface->surface->events.commit,
		&roots_surface->surface_commit);
	roots_surface->destroy.notify = handle_destroy;
	wl_signal_add(&layer_surface->events.destroy, &roots_surface->destroy);
	roots_surface->map.notify = handle_map;
	wl_signal_add(&layer_surface->events.map, &roots_surface->map);
	roots_surface->unmap.notify = handle_unmap;
	wl_signal_add(&layer_surface->events.unmap, &roots_surface->unmap);

	roots_surface->layer_surface = layer_surface;

	wl_list_insert(&desktop->layers[layer_surface->layer], &roots_surface->link);
}
