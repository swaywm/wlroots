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

struct wlr_layer_surface *layer_surface_get_exclusive(
		struct roots_desktop *desktop, uint32_t input_devices) {
	struct wlr_layer_surface *layer_surface = NULL;
	int32_t layer = -1;

	struct wlr_layer_surface *_layer_surface;
	wl_list_for_each(_layer_surface, &desktop->surface_layers->surfaces, link) {
		if (_layer_surface->exclusive_types & input_devices &&
				(int32_t)_layer_surface->layer > layer) {
			layer = _layer_surface->layer;
			layer_surface = _layer_surface;
		}
	}

	return layer_surface;
}

void handle_surface_layers_surface(struct wl_listener *listener, void *data) {
	//struct roots_desktop *desktop =
	//	wl_container_of(listener, desktop, surface_layers_surface);

	struct wlr_layer_surface *surface = data;
	wlr_log(L_DEBUG, "new surface_layers surface at layer %d", surface->layer);

	// TODO
}
