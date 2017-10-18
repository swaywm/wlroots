#define _XOPEN_SOURCE
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
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

struct wl_seat *seat;
struct wl_pointer *pointer;
struct wl_keyboard *keyboard;

uint32_t width = 300, height = 200;

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

	if (eglSwapBuffers(egl_display, egl_surface)) {
		fprintf(stderr, "Swapped buffers\n");
	} else {
		fprintf(stderr, "Swapped buffers failed\n");
	}
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
	} else {
		fprintf(stderr, "Created egl window\n");
	}

	egl_surface = eglCreateWindowSurface(egl_display, egl_conf,
		(EGLNativeWindowType)egl_window, NULL);

	draw();

	wl_display_dispatch(display);
	wl_display_roundtrip(display);
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

int parse_pair(const char *s, uint32_t *x, uint32_t *y) {
	char *sep;
	long lx = strtol(s, &sep, 10);
	if (s == sep || *sep != ',' || lx < 0 || lx > INT_MAX) {
		return 1;
	}

	char *end;
	long ly = strtol(sep + 1, &end, 10);
	if (sep + 1 == end || *end != '\0'|| ly < 0 || ly > INT_MAX) {
		return 1;
	}

	*x = (uint32_t)lx;
	*y = (uint32_t)ly;
	return 0;
}

int main(int argc, char **argv) {
	enum surface_layers_layer layer_index = SURFACE_LAYERS_LAYER_OVERLAY;
	uint32_t anchor = LAYER_SURFACE_ANCHOR_NONE;
	uint32_t input_types = LAYER_SURFACE_INPUT_DEVICE_NONE;
	uint32_t exclusive_types = LAYER_SURFACE_INPUT_DEVICE_NONE;
	uint32_t margin_horizontal = 0, margin_vertical = 0;
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
			err = parse_pair(optarg, &width, &height);
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

	layer_surface_set_anchor(layer, anchor);
	layer_surface_set_interactivity(layer, input_types, exclusive_types);
	layer_surface_set_margin(layer, margin_horizontal, margin_vertical);
	// TODO: layer_surface_set_exclusive_zone

	init_egl();
	create_window();

	callback = wl_display_sync(display);
	wl_callback_add_listener(callback, &configure_callback_listener, NULL);

	while (wl_display_dispatch(display) != -1) {}

	wl_display_disconnect(display);
	printf("disconnected from display\n");

	exit(EXIT_SUCCESS);
}
