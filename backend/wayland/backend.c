#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>

#include <wlr/config.h>

#include <wayland-server-core.h>

#include <wlr/backend/interface.h>
#include <wlr/interfaces/wlr_input_device.h>
#include <wlr/interfaces/wlr_output.h>
#include <wlr/render/egl.h>
#include <wlr/render/gles2.h>
#include <wlr/util/log.h>

#include "backend/wayland.h"
#include "util/signal.h"
#include "xdg-decoration-unstable-v1-client-protocol.h"
#include "pointer-gestures-unstable-v1-client-protocol.h"
#include "xdg-shell-client-protocol.h"
#include "tablet-unstable-v2-client-protocol.h"

struct wlr_wl_backend *get_wl_backend_from_backend(struct wlr_backend *backend) {
	assert(wlr_backend_is_wl(backend));
	return (struct wlr_wl_backend *)backend;
}

static int dispatch_events(int fd, uint32_t mask, void *data) {
	struct wlr_wl_backend *wl = data;

	if ((mask & WL_EVENT_HANGUP) || (mask & WL_EVENT_ERROR)) {
		wl_display_terminate(wl->local_display);
		return 0;
	}

	if (mask & WL_EVENT_READABLE) {
		return wl_display_dispatch(wl->remote_display);
	}
	if (mask & WL_EVENT_WRITABLE) {
		wl_display_flush(wl->remote_display);
		return 0;
	}
	if (mask == 0) {
		int count = wl_display_dispatch_pending(wl->remote_display);
		wl_display_flush(wl->remote_display);
		return count;
	}

	return 0;
}

static void xdg_wm_base_handle_ping(void *data,
		struct xdg_wm_base *base, uint32_t serial) {
	xdg_wm_base_pong(base, serial);
}

static const struct xdg_wm_base_listener xdg_wm_base_listener = {
	xdg_wm_base_handle_ping,
};

static void registry_global(void *data, struct wl_registry *registry,
		uint32_t name, const char *iface, uint32_t version) {
	struct wlr_wl_backend *wl = data;

	wlr_log(WLR_DEBUG, "Remote wayland global: %s v%d", iface, version);

	if (strcmp(iface, wl_compositor_interface.name) == 0) {
		wl->compositor = wl_registry_bind(registry, name,
			&wl_compositor_interface, 4);
	} else if (strcmp(iface, wl_seat_interface.name) == 0) {
		wl->seat = wl_registry_bind(registry, name,
			&wl_seat_interface, 5);
		wl_seat_add_listener(wl->seat, &seat_listener, wl);
	} else if (strcmp(iface, xdg_wm_base_interface.name) == 0) {
		wl->xdg_wm_base = wl_registry_bind(registry, name,
			&xdg_wm_base_interface, 1);
		xdg_wm_base_add_listener(wl->xdg_wm_base, &xdg_wm_base_listener, NULL);
	} else if (strcmp(iface, zxdg_decoration_manager_v1_interface.name) == 0) {
		wl->zxdg_decoration_manager_v1 = wl_registry_bind(registry, name,
			&zxdg_decoration_manager_v1_interface, 1);
	} else if (strcmp(iface, zwp_pointer_gestures_v1_interface.name) == 0) {
		wl->zwp_pointer_gestures_v1 = wl_registry_bind(registry, name,
			&zwp_pointer_gestures_v1_interface, 1);
	} else if (strcmp(iface, zwp_tablet_manager_v2_interface.name) == 0) {
		wl->tablet_manager = wl_registry_bind(registry, name,
			&zwp_tablet_manager_v2_interface, 1);
	}
}

static void registry_global_remove(void *data, struct wl_registry *registry,
		uint32_t name) {
	// TODO
}

static const struct wl_registry_listener registry_listener = {
	.global = registry_global,
	.global_remove = registry_global_remove
};

/*
 * Initializes the wayland backend. Opens a connection to a remote wayland
 * compositor and creates surfaces for each output, then registers globals on
 * the specified display.
 */
static bool backend_start(struct wlr_backend *backend) {
	struct wlr_wl_backend *wl = get_wl_backend_from_backend(backend);
	wlr_log(WLR_INFO, "Initializating wayland backend");

	wl->started = true;

	if (wl->keyboard) {
		create_wl_keyboard(wl->keyboard, wl);
	}

	if (wl->tablet_manager && wl->seat) {
		wl_add_tablet_seat(wl->tablet_manager,
			wl->seat, wl);
	}

	for (size_t i = 0; i < wl->requested_outputs; ++i) {
		wlr_wl_output_create(&wl->backend);
	}

	return true;
}

static void backend_destroy(struct wlr_backend *backend) {
	if (!backend) {
		return;
	}

	struct wlr_wl_backend *wl = get_wl_backend_from_backend(backend);

	struct wlr_wl_output *output, *tmp_output;
	wl_list_for_each_safe(output, tmp_output, &wl->outputs, link) {
		wlr_output_destroy(&output->wlr_output);
	}

	struct wlr_input_device *input_device, *tmp_input_device;
	wl_list_for_each_safe(input_device, tmp_input_device, &wl->devices, link) {
		wlr_input_device_destroy(input_device);
	}

	wlr_signal_emit_safe(&wl->backend.events.destroy, &wl->backend);

	wl_list_remove(&wl->local_display_destroy.link);

	free(wl->seat_name);

	wl_event_source_remove(wl->remote_display_src);

	wlr_renderer_destroy(wl->renderer);
	wlr_egl_finish(&wl->egl);

	if (wl->pointer) {
		wl_pointer_destroy(wl->pointer);
	}
	if (wl->seat) {
		wl_seat_destroy(wl->seat);
	}
	if (wl->zxdg_decoration_manager_v1) {
		zxdg_decoration_manager_v1_destroy(wl->zxdg_decoration_manager_v1);
	}
	if (wl->zwp_pointer_gestures_v1) {
		zwp_pointer_gestures_v1_destroy(wl->zwp_pointer_gestures_v1);
	}
	xdg_wm_base_destroy(wl->xdg_wm_base);
	wl_compositor_destroy(wl->compositor);
	wl_registry_destroy(wl->registry);
	wl_display_disconnect(wl->remote_display);
	free(wl);
}

static struct wlr_renderer *backend_get_renderer(struct wlr_backend *backend) {
	struct wlr_wl_backend *wl = get_wl_backend_from_backend(backend);
	return wl->renderer;
}

static struct wlr_backend_impl backend_impl = {
	.start = backend_start,
	.destroy = backend_destroy,
	.get_renderer = backend_get_renderer,
};

bool wlr_backend_is_wl(struct wlr_backend *b) {
	return b->impl == &backend_impl;
}

static void handle_display_destroy(struct wl_listener *listener, void *data) {
	struct wlr_wl_backend *wl =
		wl_container_of(listener, wl, local_display_destroy);
	backend_destroy(&wl->backend);
}

struct wlr_backend *wlr_wl_backend_create(struct wl_display *display,
		const char *remote, wlr_renderer_create_func_t create_renderer_func) {
	wlr_log(WLR_INFO, "Creating wayland backend");

	struct wlr_wl_backend *wl = calloc(1, sizeof(*wl));
	if (!wl) {
		wlr_log_errno(WLR_ERROR, "Allocation failed");
		return NULL;
	}

	wlr_backend_init(&wl->backend, &backend_impl);

	wl->local_display = display;
	wl_list_init(&wl->devices);
	wl_list_init(&wl->outputs);

	wl->remote_display = wl_display_connect(remote);
	if (!wl->remote_display) {
		wlr_log_errno(WLR_ERROR, "Could not connect to remote display");
		goto error_wl;
	}

	wl->registry = wl_display_get_registry(wl->remote_display);
	if (!wl->registry) {
		wlr_log_errno(WLR_ERROR, "Could not obtain reference to remote registry");
		goto error_display;
	}

	wl_registry_add_listener(wl->registry, &registry_listener, wl);
	wl_display_dispatch(wl->remote_display);
	wl_display_roundtrip(wl->remote_display);

	if (!wl->compositor) {
		wlr_log(WLR_ERROR,
			"Remote Wayland compositor does not support wl_compositor");
		goto error_registry;
	}
	if (!wl->xdg_wm_base) {
		wlr_log(WLR_ERROR,
			"Remote Wayland compositor does not support xdg-shell");
		goto error_registry;
	}

	struct wl_event_loop *loop = wl_display_get_event_loop(wl->local_display);
	int fd = wl_display_get_fd(wl->remote_display);
	int events = WL_EVENT_READABLE | WL_EVENT_ERROR | WL_EVENT_HANGUP;
	wl->remote_display_src = wl_event_loop_add_fd(loop, fd, events,
		dispatch_events, wl);
	if (!wl->remote_display_src) {
		wlr_log(WLR_ERROR, "Failed to create event source");
		goto error_registry;
	}
	wl_event_source_check(wl->remote_display_src);

	static EGLint config_attribs[] = {
		EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
		EGL_RED_SIZE, 1,
		EGL_GREEN_SIZE, 1,
		EGL_BLUE_SIZE, 1,
		EGL_ALPHA_SIZE, 1,
		EGL_NONE,
	};

	if (!create_renderer_func) {
		create_renderer_func = wlr_renderer_autocreate;
	}

	wl->renderer = create_renderer_func(&wl->egl, EGL_PLATFORM_WAYLAND_EXT,
		wl->remote_display, config_attribs, WL_SHM_FORMAT_ARGB8888);

	if (!wl->renderer) {
		wlr_log(WLR_ERROR, "Could not create renderer");
		goto error_event;
	}

	wl->local_display_destroy.notify = handle_display_destroy;
	wl_display_add_destroy_listener(display, &wl->local_display_destroy);

	return &wl->backend;

error_event:
	wl_event_source_remove(wl->remote_display_src);
error_registry:
	if (wl->compositor) {
		wl_compositor_destroy(wl->compositor);
	}
	if (wl->xdg_wm_base) {
		xdg_wm_base_destroy(wl->xdg_wm_base);
	}
	wl_registry_destroy(wl->registry);
error_display:
	wl_display_disconnect(wl->remote_display);
error_wl:
	free(wl);
	return NULL;
}
