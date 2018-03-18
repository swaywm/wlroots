#define _POSIX_C_SOURCE 2
#include <GLES2/gl2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wayland-client.h>
#include <wayland-egl.h>
#include <wlr/render/egl.h>
#include <wlr/util/log.h>
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

static struct wl_compositor *compositor = NULL;
static struct zwlr_layer_shell_v1 *layer_shell = NULL;
static struct wl_output *wl_output = NULL;

struct wl_surface *wl_surface;
struct wlr_egl egl;
struct wl_egl_window *egl_window;
struct wlr_egl_surface *egl_surface;
struct wl_callback *frame_callback;

static uint32_t output = 0;
static uint32_t layer = -1;
static uint32_t anchor = 0;
static uint32_t width = 256, height = 256;

static void draw(void);

static void surface_frame_callback(
		void *data, struct wl_callback *cb, uint32_t time) {
	wl_callback_destroy(cb);
	frame_callback = NULL;
	draw();
}

static struct wl_callback_listener frame_listener = {
	.done = surface_frame_callback
};

static void draw(void) {
	eglMakeCurrent(egl.display, egl_surface, egl_surface, egl.context);
	wlr_log(L_DEBUG, "Drawing frame");

	float color[] = {1.0, 0.0, 0.0, 1.0};
	glViewport(0, 0, width, height);
	glClearColor(color[0], color[1], color[2], 1.0);
	glClear(GL_COLOR_BUFFER_BIT);

	eglSwapBuffers(egl.display, egl_surface);

	frame_callback = wl_surface_frame(wl_surface);
	wl_callback_add_listener(frame_callback, &frame_listener, NULL);
}

static void layer_surface_configure(void *data,
		struct zwlr_layer_surface_v1 *surface,
		uint32_t serial, uint32_t w, uint32_t h) {
	width = w;
	height = h;
	zwlr_layer_surface_v1_ack_configure(surface, serial);
}

struct zwlr_layer_surface_v1_listener layer_surface_listener = {
	.configure = layer_surface_configure,
};

static void handle_global(void *data, struct wl_registry *registry,
		uint32_t name, const char *interface, uint32_t version) {
	if (strcmp(interface, "wl_compositor") == 0) {
		compositor = wl_registry_bind(registry, name,
				&wl_compositor_interface, 1);
	} else if (strcmp(interface, "wl_output") == 0) {
		if (output == 0) {
			wl_output = wl_registry_bind(registry, name,
					&wl_output_interface, 1);
		} else {
			output--;
		}
	} else if (strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
		layer_shell = wl_registry_bind(
				registry, name, &zwlr_layer_shell_v1_interface, 1);
	}
}

static void handle_global_remove(void *data, struct wl_registry *registry,
		uint32_t name) {
	// who cares
}

static const struct wl_registry_listener registry_listener = {
	.global = handle_global,
	.global_remove = handle_global_remove,
};

int main(int argc, char **argv) {
	wlr_log_init(L_DEBUG, NULL);
	char *namespace = "wlroots";
	bool found;
	int c;
	while ((c = getopt(argc, argv, "w:h:o:"))) {
		switch (c) {
		case 'o':
			output = atoi(optarg);
			break;
		case 'w':
			width = atoi(optarg);
			break;
		case 'h':
			height = atoi(optarg);
			break;
		case 'l': {
			struct {
				char *name;
				enum zwlr_layer_shell_v1_layer value;
			} layers[] = {
				{ "background", ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND },
				{ "bottom", ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM },
				{ "top", ZWLR_LAYER_SHELL_V1_LAYER_TOP },
				{ "overlay", ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY },
			};
			found = false;
			for (size_t i = 0; i < sizeof(layers) / sizeof(layers[0]); ++i) {
				if (strcmp(optarg, layers[i].name)) {
					layer = layers[i].value;
					found = true;
					break;
				}
			}
			if (!found) {
				fprintf(stderr, "invalid layer %s\n", optarg);
				return 1;
			}
			break;
		}
		case 'a': {
			struct {
				char *name;
				uint32_t value;
			} anchors[] = {
				{ "top", ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP },
				{ "bottom", ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM },
				{ "left", ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT },
				{ "right", ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT },
			};
			found = false;
			for (size_t i = 0; i < sizeof(anchors) / sizeof(anchors[0]); ++i) {
				if (strcmp(optarg, anchors[i].name)) {
					anchor |= anchors[i].value;
					found = true;
					break;
				}
			}
			if (!found) {
				fprintf(stderr, "invalid anchor %s\n", optarg);
				return 1;
			}
		}
		default:
			break;
		}
	}

	struct wl_display *display = wl_display_connect(NULL);
	if (display == NULL) {
		fprintf(stderr, "Failed to create display\n");
		return 1;
	}

	struct wl_registry *registry = wl_display_get_registry(display);
	wl_registry_add_listener(registry, &registry_listener, NULL);
	wl_display_dispatch(display);
	wl_display_roundtrip(display);

	if (compositor == NULL) {
		fprintf(stderr, "wl-compositor not available\n");
		return 1;
	}
	if (layer_shell == NULL) {
		fprintf(stderr, "layer-shell not available\n");
		return 1;
	}
	if (wl_output == NULL) {
		fprintf(stderr, "wl_output not available\n");
		return 1;
	}

	wlr_egl_init(&egl, EGL_PLATFORM_WAYLAND_EXT, display, NULL,
		WL_SHM_FORMAT_ARGB8888);

	wl_surface = wl_compositor_create_surface(compositor);

	struct zwlr_layer_surface_v1 *layer_surface =
		zwlr_layer_shell_v1_get_layer_surface(layer_shell,
				wl_surface, wl_output, layer, namespace);
	zwlr_layer_surface_v1_set_anchor(layer_surface, anchor);
	// TODO: margin, interactivity, exclusive zone

	wl_surface_commit(wl_surface);

	egl_window = wl_egl_window_create(wl_surface, width, height);
	egl_surface = wlr_egl_create_surface(&egl, egl_window);

	wl_display_roundtrip(display);
	draw();

	while (wl_display_dispatch(display) != -1) {
		// This space intentionally left blank
	}
	return 0;
}
