#define _XOPEN_SOURCE
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <time.h>
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
struct surface_layers *layers = NULL;

struct wl_output *output = NULL;
uint32_t output_id;

struct wl_surface *surface = NULL;
struct layer_surface *layer = NULL;
struct wl_egl_window *egl_window = NULL;

struct wl_seat *seat = NULL;
struct wl_pointer *pointer = NULL;
struct wl_keyboard *keyboard = NULL;

EGLDisplay egl_display;
EGLConfig egl_conf;
EGLSurface egl_surface;
EGLContext egl_context;

enum surface_layers_layer layer_index = SURFACE_LAYERS_LAYER_OVERLAY;
uint32_t anchor = LAYER_SURFACE_ANCHOR_NONE;
uint32_t input_types = LAYER_SURFACE_INPUT_DEVICE_NONE;
uint32_t exclusive_types = LAYER_SURFACE_INPUT_DEVICE_NONE;
int32_t margin_horizontal = 0, margin_vertical = 0;
uint32_t width = 300, height = 200;
float color[4] = {1.0, 0.0, 0.0, 1.0};

static void draw() {
	int ok = eglMakeCurrent(egl_display, egl_surface, egl_surface, egl_context);
	if (!ok) {
		fprintf(stderr, "Make current failed\n");
		return;
	}

	glClearColor(color[0], color[1], color[2], color[3]);
	glClear(GL_COLOR_BUFFER_BIT);

	ok = eglSwapBuffers(egl_display, egl_surface);
	if (!ok) {
		fprintf(stderr, "Swapped buffers failed\n");
		return;
	}

	printf("Rendered surface %p\n", surface);
}

static void pointer_handle_enter(void *data, struct wl_pointer *pointer,
		uint32_t serial, struct wl_surface *surface, wl_fixed_t _sx,
		wl_fixed_t _sy) {
	double sx = wl_fixed_to_double(_sx);
	double sy = wl_fixed_to_double(_sy);
	printf("Pointer entered surface %p at %f,%f\n", surface, sx, sy);
}

static void pointer_handle_leave(void *data, struct wl_pointer *pointer,
		uint32_t serial, struct wl_surface *surface) {
	printf("Pointer left surface %p\n", surface);
}

static void pointer_handle_motion(void *data, struct wl_pointer *pointer,
		uint32_t time, wl_fixed_t _sx, wl_fixed_t _sy) {
	double sx = wl_fixed_to_double(_sx);
	double sy = wl_fixed_to_double(_sy);
	printf("Pointer moved at %f,%f\n", sx, sy);
}

static void pointer_handle_button(void *data, struct wl_pointer *wl_pointer,
		uint32_t serial, uint32_t time, uint32_t button, uint32_t state) {
	printf("Pointer button: button=%d state=%d\n", button, state);
}

static void pointer_handle_axis(void *data, struct wl_pointer *wl_pointer,
		uint32_t time, uint32_t axis, wl_fixed_t value) {
	printf("Pointer axis: axis=%d value=%d\n", axis, value);
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

static void keyboard_handle_keymap(void *data, struct wl_keyboard *wl_keyboard,
		enum wl_keyboard_keymap_format format, int fd, uint32_t size) {}

static void keyboard_handle_enter(void *data, struct wl_keyboard *wl_keyboard,
		uint32_t serial, struct wl_surface *surface, struct wl_array *keys) {
	printf("Keyboard entered surface %p\n", surface);
}

static void keyboard_handle_leave(void *data, struct wl_keyboard *wl_keyboard,
		uint32_t serial, struct wl_surface *surface) {
	printf("Keyboard left surface %p\n", surface);
}

static void keyboard_handle_key(void *data, struct wl_keyboard *wl_keyboard,
		uint32_t serial, uint32_t time, uint32_t key,
		enum wl_keyboard_key_state state) {
	printf("Keyboard key: key=%d state=%d\n", key, state);
}

static void keyboard_handle_modifiers(void *data,
		struct wl_keyboard *wl_keyboard, uint32_t serial,
		uint32_t mods_depressed, uint32_t mods_latched, uint32_t mods_locked,
		uint32_t group) {}

static void keyboard_handle_repeat_info(void *data,
		struct wl_keyboard *wl_keyboard, int32_t rate, int32_t delay) {}

static const struct wl_keyboard_listener keyboard_listener = {
	.keymap = keyboard_handle_keymap,
	.enter = keyboard_handle_enter,
	.leave = keyboard_handle_leave,
	.key = keyboard_handle_key,
	.modifiers = keyboard_handle_modifiers,
	.repeat_info = keyboard_handle_repeat_info,
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

	if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !keyboard) {
		keyboard = wl_seat_get_keyboard(seat);
		wl_keyboard_add_listener(keyboard, &keyboard_listener, NULL);
	} else if (!(caps & WL_SEAT_CAPABILITY_POINTER) && keyboard) {
		wl_keyboard_destroy(keyboard);
		keyboard = NULL;
	}
}

static void seat_handle_name(void *data, struct wl_seat *wl_seat,
		const char *name) {}

static const struct wl_seat_listener seat_listener = {
	.capabilities = seat_handle_capabilities,
	.name = seat_handle_name,
};

void registry_handle_global_add(void *data, struct wl_registry *registry,
		uint32_t id, const char *interface, uint32_t version) {
	if (strcmp(interface, "wl_compositor") == 0) {
		compositor =
			wl_registry_bind(registry, id, &wl_compositor_interface, 1);
	} else if (strcmp(interface, "wl_seat") == 0) {
		seat = wl_registry_bind(registry, id, &wl_seat_interface, 1);
		wl_seat_add_listener(seat, &seat_listener, NULL);
	} else if (strcmp(interface, "wl_output") == 0 && output == NULL) {
		output = wl_registry_bind(registry, id, &wl_output_interface, 1);
		output_id = id;
	} else if (strcmp(interface, "surface_layers") == 0) {
		layers = wl_registry_bind(registry, id, &surface_layers_interface, 1);
	}
}

static void registry_handle_global_remove(void *data,
		struct wl_registry *registry, uint32_t id) {
	if (output_id == id) {
		wl_output_destroy(output);
		output = NULL;
	}
}

static const struct wl_registry_listener registry_listener = {
	.global = registry_handle_global_add,
	.global_remove = registry_handle_global_remove,
};

static void layer_surface_handle_configure(void *data,
		struct layer_surface *layer, uint32_t width, uint32_t height) {
	printf("Got a layer surface configure event with size %dx%d\n", width,
		height);
	wl_egl_window_resize(egl_window, width, height, 0, 0);
	draw();
}

static const struct layer_surface_listener layer_listener = {
	.configure = layer_surface_handle_configure,
};

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

static void create_window() {
	egl_window = wl_egl_window_create(surface, width, height);
	if (egl_window == EGL_NO_SURFACE) {
		fprintf(stderr, "Can't create egl window\n");
		exit(EXIT_FAILURE);
	}
	fprintf(stderr, "Created egl window\n");

	egl_surface = eglCreateWindowSurface(egl_display, egl_conf,
		(EGLNativeWindowType)egl_window, NULL);

	draw();
}

static void destroy_window() {
	eglDestroySurface(egl_display, egl_surface);
	wl_egl_window_destroy(egl_window);
	eglDestroyContext(egl_display, egl_context);

	fprintf(stderr, "Destroyed egl window\n");
}

void create_surface() {
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

	layer_surface_add_listener(layer, &layer_listener, NULL);

	layer_surface_set_anchor(layer, anchor);
	layer_surface_set_interactivity(layer, input_types, exclusive_types);
	layer_surface_set_margin(layer, margin_horizontal, margin_vertical);
	// TODO: layer_surface_set_exclusive_zone
	// create_window will commit the layer surface state

	init_egl();
	create_window();
}

void destroy_surface() {
	if (surface == NULL) {
		return;
	}

	destroy_window();

	layer_surface_destroy(layer);
	wl_surface_destroy(surface);
	surface = NULL;
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

enum layer_surface_anchor parse_anchor(const char *s) {
	if (strcmp(s, "top") == 0) {
		return LAYER_SURFACE_ANCHOR_TOP;
	} else if (strcmp(s, "bottom") == 0) {
		return LAYER_SURFACE_ANCHOR_BOTTOM;
	} else if (strcmp(s, "left") == 0) {
		return LAYER_SURFACE_ANCHOR_LEFT;
	} else if (strcmp(s, "right") == 0) {
		return LAYER_SURFACE_ANCHOR_RIGHT;
	}
	fprintf(stderr, "Unknown anchor name: %s\n", s);
	exit(EXIT_FAILURE);
}

enum layer_surface_input_device parse_input_device(const char *s) {
	if (strcmp(s, "pointer") == 0) {
		return LAYER_SURFACE_INPUT_DEVICE_POINTER;
	} else if (strcmp(s, "keyboard") == 0) {
		return LAYER_SURFACE_INPUT_DEVICE_KEYBOARD;
	} else if (strcmp(s, "touch") == 0) {
		return LAYER_SURFACE_INPUT_DEVICE_TOUCH;
	}
	fprintf(stderr, "Unknown input device: %s\n", s);
	exit(EXIT_FAILURE);
}

int parse_pair(const char *s, int32_t *x, int32_t *y) {
	errno = 0;

	char *sep;
	long lx = strtol(s, &sep, 10);
	if (s == sep || *sep != ',' || errno == EINVAL || errno == ERANGE) {
		return 1;
	}

	char *end;
	long ly = strtol(sep + 1, &end, 10);
	if (sep + 1 == end || *end != '\0' || errno == EINVAL || errno == ERANGE) {
		return 1;
	}

	*x = (uint32_t)lx;
	*y = (uint32_t)ly;
	return 0;
}

int main(int argc, char **argv) {
	while (1) {
		int opt = getopt(argc, argv, "l:a:i:e:m:s:h");
		if (opt == -1) {
			break;
		}

		int err;
		switch (opt) {
		case 'l':
			layer_index = parse_layer(optarg);
			break;
		case 'a':
			anchor |= parse_anchor(optarg);
			break;
		case 'i':
			input_types |= parse_input_device(optarg);
			break;
		case 'e':
			exclusive_types |= parse_input_device(optarg);
			break;
		case 'm':
			err = parse_pair(optarg, &margin_horizontal, &margin_vertical);
			if (err) {
				fprintf(stderr, "Invalid margin: %s\n", optarg);
				exit(EXIT_FAILURE);
			}
			break;
		case 's':
			err = parse_pair(optarg, (int32_t *)&width, (int32_t *)&height);
			if (err) {
				fprintf(stderr, "Invalid size: %s\n", optarg);
				exit(EXIT_FAILURE);
			}
			break;
		case 'h':
		default:
			fprintf(stderr, "Usage: %s [-l <layer>] [-a <anchor>]... "
				"[-i <input-type>]... [-e <exclusive-type>]... "
				"[-m <margin_h,margin_v>] [-s <width,height>] [-h]\n", argv[0]);
			exit(EXIT_FAILURE);
		}
	}

	srand((unsigned) time(NULL));
	color[0] = (float)rand() / RAND_MAX;
	color[1] = (float)rand() / RAND_MAX;
	color[2] = (float)rand() / RAND_MAX;

	display = wl_display_connect(NULL);
	if (display == NULL) {
		fprintf(stderr, "Can't connect to display\n");
		exit(EXIT_FAILURE);
	}
	printf("Connected to display\n");

	struct wl_registry *registry = wl_display_get_registry(display);
	wl_registry_add_listener(registry, &registry_listener, NULL);

	wl_display_dispatch(display);
	wl_display_roundtrip(display);

	if (compositor == NULL) {
		fprintf(stderr, "Can't find compositor\n");
		exit(EXIT_FAILURE);
	}
	if (layers == NULL) {
		fprintf(stderr, "Can't find surface_layers\n");
		exit(EXIT_FAILURE);
	}

	do {
		if (output != NULL && surface == NULL) {
			create_surface();
		}
		if (output == NULL && surface != NULL) {
			destroy_surface();
		}
	} while (wl_display_dispatch(display) != -1);

	destroy_surface();

	wl_display_disconnect(display);
	printf("Disconnected from display\n");

	exit(EXIT_SUCCESS);
}
