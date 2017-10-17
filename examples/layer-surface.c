#define _XOPEN_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <wayland-client-protocol.h>
#include <wayland-client.h>
#include <wayland-egl.h>
#include <wayland-server.h>
#include <linux/input-event-codes.h>
#include "surface-layers-client-protocol.h"

struct wl_display *display = NULL;
struct wl_compositor *compositor = NULL;
struct wl_output *output = NULL;
struct surface_layers *layers = NULL;

struct wl_surface *surface;
struct layer_surface *layer;
struct wl_egl_window *egl_window;
struct wl_region *region;
struct wl_callback *callback;

// input devices
struct wl_seat *seat;
struct wl_pointer *pointer;

EGLDisplay egl_display;
EGLConfig egl_conf;
EGLSurface egl_surface;
EGLContext egl_context;

static void draw() {
	if (eglMakeCurrent(egl_display, egl_surface, egl_surface, egl_context)) {
		fprintf(stderr, "Made current\n");
	} else {
		fprintf(stderr, "Made current failed\n");
	}

	glClearColor(1.0, 1.0, 0.0, 0.1);
	glClear(GL_COLOR_BUFFER_BIT);
	glFlush();

	if (eglSwapBuffers(egl_display, egl_surface)) {
		fprintf(stderr, "Swapped buffers\n");
	} else {
		fprintf(stderr, "Swapped buffers failed\n");
	}
}

static void pointer_handle_enter(void *data, struct wl_pointer *pointer,
		uint32_t serial, struct wl_surface *surface, wl_fixed_t sx,
		wl_fixed_t sy) {
	fprintf(stderr, "Pointer entered surface %p at %d %d\n", surface, sx, sy);
}

static void pointer_handle_leave(void *data, struct wl_pointer *pointer,
		uint32_t serial, struct wl_surface *surface) {
	fprintf(stderr, "Pointer left surface %p\n", surface);
}

static void pointer_handle_motion(void *data, struct wl_pointer *pointer,
		uint32_t time, wl_fixed_t sx, wl_fixed_t sy) {
	printf("Pointer moved at %d %d\n", sx, sy);
}

static void pointer_handle_button(void *data, struct wl_pointer *wl_pointer,
		uint32_t serial, uint32_t time, uint32_t button, uint32_t state) {
	printf("Pointer button: button=%d state=%d\n", button, state);
}

static void pointer_handle_axis(void *data, struct wl_pointer *wl_pointer,
		uint32_t time, uint32_t axis, wl_fixed_t value) {
	printf("Pointer handle axis\n");
}

static void pointer_handle_frame(void *data, struct wl_pointer *wl_pointer) {}

static const struct wl_pointer_listener pointer_listener = {
	.enter = pointer_handle_enter,
	.leave = pointer_handle_leave,
	.motion = pointer_handle_motion,
	.button = pointer_handle_button,
	.axis = pointer_handle_axis,
	.frame = pointer_handle_frame,
};

static void seat_handle_capabilities(void *data, struct wl_seat *seat,
		enum wl_seat_capability caps) {
	if ((caps & WL_SEAT_CAPABILITY_POINTER) && !pointer) {
		pointer = wl_seat_get_pointer(seat);
		wl_pointer_add_listener(pointer, &pointer_listener, NULL);
	} else if (!(caps & WL_SEAT_CAPABILITY_POINTER) && pointer) {
		wl_pointer_destroy(pointer);
		pointer = NULL;
	}
}

static void seat_handle_name(void *data, struct wl_seat *wl_seat,
		const char *name) {}

static const struct wl_seat_listener seat_listener = {
	.capabilities = seat_handle_capabilities,
	.name = seat_handle_name,
};

void global_registry_handler(void *data, struct wl_registry *registry,
		uint32_t id, const char *interface, uint32_t version) {
	printf("Got a registry event for %s id %d\n", interface, id);
	if (strcmp(interface, "wl_compositor") == 0) {
		compositor =
			wl_registry_bind(registry, id, &wl_compositor_interface, 1);
	} else if (strcmp(interface, "wl_seat") == 0) {
		seat = wl_registry_bind(registry, id, &wl_seat_interface, 1);
		wl_seat_add_listener(seat, &seat_listener, NULL);
	} else if (strcmp(interface, "wl_output") == 0) {
		output = wl_registry_bind(registry, id, &wl_output_interface, 1);
	} else if (strcmp(interface, "surface_layers") == 0) {
		layers = wl_registry_bind(registry, id, &surface_layers_interface, 1);
	}
}

static void global_registry_remover(void *data, struct wl_registry *registry,
		uint32_t id) {
	printf("Got a registry losing event for %d\n", id);
}

static const struct wl_registry_listener registry_listener = {
	global_registry_handler, global_registry_remover,
};

static void redraw(void *data, struct wl_callback *callback, uint32_t time) {
	printf("Redrawing\n");
}

//static const struct wl_callback_listener frame_listener = {
//	.done = redraw
//};

static void configure_callback(void *data, struct wl_callback *callback,
		uint32_t time) {
	if (callback == NULL) {
		redraw(data, NULL, time);
	}
}

static struct wl_callback_listener configure_callback_listener = {
	.done = configure_callback,
};

static void create_window() {
	egl_window = wl_egl_window_create(surface, 480, 360);
	if (egl_window == EGL_NO_SURFACE) {
		fprintf(stderr, "Can't create egl window\n");
		exit(EXIT_FAILURE);
	} else {
		fprintf(stderr, "Created egl window\n");
	}

	egl_surface = eglCreateWindowSurface(egl_display, egl_conf,
		(EGLNativeWindowType)egl_window, NULL);

	draw();

	wl_display_dispatch(display);
	wl_display_roundtrip(display);
}

static void init_egl() {
	EGLint config_attribs[] = {
		EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
		EGL_RED_SIZE, 8,
		EGL_GREEN_SIZE, 8,
		EGL_BLUE_SIZE, 8,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
		EGL_NONE,
	};

	static const EGLint context_attribs[] = {
		EGL_CONTEXT_CLIENT_VERSION, 2,
		EGL_NONE,
	};

	egl_display = eglGetDisplay((EGLNativeDisplayType)display);
	if (egl_display == EGL_NO_DISPLAY) {
		fprintf(stderr, "Can't create egl display\n");
		exit(EXIT_FAILURE);
	} else {
		fprintf(stderr, "Created egl display\n");
	}

	EGLint major, minor;
	if (eglInitialize(egl_display, &major, &minor) != EGL_TRUE) {
		fprintf(stderr, "Can't initialise egl display\n");
		exit(EXIT_FAILURE);
	}
	printf("EGL major: %d, minor %d\n", major, minor);

	if (!eglBindAPI(EGL_OPENGL_ES_API)) {
		fprintf(stderr, "Can't bind API\n");
		exit(EXIT_FAILURE);
	} else {
		fprintf(stderr, "Bound API\n");
	}

	EGLint count;
	eglGetConfigs(egl_display, NULL, 0, &count);
	printf("EGL has %d configs\n", count);

	EGLConfig *configs = calloc(count, sizeof(EGLConfig));
	if (configs == NULL) {
		fprintf(stderr, "Can't allocate EGL configs\n");
		exit(EXIT_FAILURE);
	}

	EGLint n;
	eglChooseConfig(egl_display, config_attribs, configs, count, &n);

	EGLint size;
	for (int i = 0; i < n; ++i) {
		eglGetConfigAttrib(egl_display, configs[i], EGL_BUFFER_SIZE, &size);
		printf("Buffer size for config %d is %d\n", i, size);
		eglGetConfigAttrib(egl_display, configs[i], EGL_RED_SIZE, &size);
		printf("Red size for config %d is %d\n", i, size);

		egl_conf = configs[i];
		break;
	}

	egl_context = eglCreateContext(egl_display, egl_conf, EGL_NO_CONTEXT,
		context_attribs);
}

enum surface_layers_layer parse_layer(const char *s) {
	if (strcmp(s, "background") == 0) {
		return SURFACE_LAYERS_LAYER_BACKGROUND;
	} else if (strcmp(s, "bottom") == 0) {
		return SURFACE_LAYERS_LAYER_BOTTOM;
	} else if (strcmp(s, "top") == 0) {
		return SURFACE_LAYERS_LAYER_TOP;
	} else if (strcmp(s, "overlay") == 0) {
		return SURFACE_LAYERS_LAYER_OVERLAY;
	}
	fprintf(stderr, "Unknown layer name: %s\n", s);
	exit(EXIT_FAILURE);
}

int main(int argc, char **argv) {
	enum surface_layers_layer layer_index = SURFACE_LAYERS_LAYER_OVERLAY;
	while (1) {
		int opt = getopt(argc, argv, "l:h");
		if (opt == -1) {
			break;
		}

		switch (opt) {
		case 'l':
			layer_index = parse_layer(optarg);
			break;
		case 'h':
		default:
			fprintf(stderr, "Usage: %s [-l <layer>] [-h]\n", argv[0]);
			exit(EXIT_FAILURE);
		}
	}

	display = wl_display_connect(NULL);
	if (display == NULL) {
		fprintf(stderr, "Can't connect to display\n");
		exit(EXIT_FAILURE);
	}
	printf("connected to display\n");

	struct wl_registry *registry = wl_display_get_registry(display);
	wl_registry_add_listener(registry, &registry_listener, NULL);

	wl_display_dispatch(display);
	wl_display_roundtrip(display);

	if (compositor == NULL) {
		fprintf(stderr, "Can't find compositor\n");
		exit(EXIT_FAILURE);
	}
	if (output == NULL) {
		fprintf(stderr, "Can't find output\n");
		exit(EXIT_FAILURE);
	}
	if (layers == NULL) {
		fprintf(stderr, "Can't find surface_layers\n");
		exit(EXIT_FAILURE);
	}

	surface = wl_compositor_create_surface(compositor);
	if (surface == NULL) {
		fprintf(stderr, "Can't create surface\n");
		exit(EXIT_FAILURE);
	} else {
		fprintf(stderr, "Created surface %p\n", surface);
	}

	layer = surface_layers_get_layer_surface(layers, surface, output,
		layer_index);
	if (!layer) {
		fprintf(stderr, "Can't create surface_layer\n");
		exit(EXIT_FAILURE);
	}

	layer_surface_set_interactivity(layer,
		LAYER_SURFACE_INPUT_DEVICES_POINTER, LAYER_SURFACE_INPUT_DEVICES_NONE);

	init_egl();
	create_window();

	callback = wl_display_sync(display);
	wl_callback_add_listener(callback, &configure_callback_listener, NULL);

	while (wl_display_dispatch(display) != -1) {}

	wl_display_disconnect(display);
	printf("disconnected from display\n");

	exit(EXIT_SUCCESS);
}
