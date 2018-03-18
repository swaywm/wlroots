#ifndef ROOTSTON_LAYERS_H
#define ROOTSTON_LAYERS_H
#include <stdbool.h>
#include <wlr/config.h>
#include <wlr/types/wlr_box.h>
#include <wlr/types/wlr_surface.h>
#include <wlr/types/wlr_layer_shell.h>

struct roots_layer_surface {
	struct wlr_layer_surface *layer_surface;
	struct wl_list link;

	struct wl_listener destroy;
	struct wl_listener map;
	struct wl_listener unmap;
	struct wl_listener surface_commit;
};

#endif
