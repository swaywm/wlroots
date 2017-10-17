#include <assert.h>
#include <stdlib.h>
#include <stdbool.h>
#include <wayland-server.h>
#include <wlr/types/wlr_box.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_surface.h>
#include <wlr/types/wlr_surface_layers.h>
#include <wlr/util/log.h>
#include "rootston/desktop.h"
#include "rootston/server.h"
#include "rootston/input.h"

void handle_surface_layers_surface(struct wl_listener *listener, void *data) {
	//struct roots_desktop *desktop =
	//	wl_container_of(listener, desktop, surface_layers_surface);

	struct wlr_layer_surface *surface = data;
	wlr_log(L_DEBUG, "new surface_layers surface at layer %d", surface->layer);

	// TODO
}
