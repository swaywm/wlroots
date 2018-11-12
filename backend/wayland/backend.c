#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

#include <wlr/config.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <wayland-server.h>

#include <wlr/backend/interface.h>
#include <wlr/interfaces/wlr_input_device.h>
#include <wlr/interfaces/wlr_output.h>
#include <wlr/render/allocator/gbm.h>
#include <wlr/render/egl.h>
#include <wlr/render/format_set.h>
#include <wlr/render/gles2.h>
#include <wlr/util/log.h>

#include "backend/wayland.h"
#include "util/signal.h"

#include "linux-dmabuf-unstable-v1-client-protocol.h"
#include "xdg-shell-client-protocol.h"

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

static void dmabuf_handle_format(void *data, struct zwp_linux_dmabuf_v1 *dmabuf,
		uint32_t format) {
	struct wlr_wl_backend *wl = data;

	if (!wlr_format_set_add(&wl->formats, format, DRM_FORMAT_MOD_INVALID)) {
		wlr_log(WLR_ERROR, "Failed to add format %#"PRIx32, format);
	}
}

static void dmabuf_handle_modifier(void *data, struct zwp_linux_dmabuf_v1 *dmabuf,
		uint32_t format, uint32_t mod_high, uint32_t mod_low) {
	struct wlr_wl_backend *wl = data;
	uint64_t mod = ((uint64_t)mod_high << 32) | mod_low;

	if (!wlr_format_set_add(&wl->formats, format, mod)) {
		wlr_log(WLR_ERROR, "Failed to add format %#"PRIx32" (%#"PRIx64")",
			format, mod);
	}
}

static const struct zwp_linux_dmabuf_v1_listener dmabuf_listener = {
	.format = dmabuf_handle_format,
	.modifier = dmabuf_handle_modifier,
};

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
			&wl_seat_interface, 2);
		wl_seat_add_listener(wl->seat, &seat_listener, wl);

	} else if (strcmp(iface, wl_shm_interface.name) == 0) {
		wl->shm = wl_registry_bind(registry, name,
			&wl_shm_interface, 1);

	} else if (strcmp(iface, xdg_wm_base_interface.name) == 0) {
		wl->xdg_wm_base = wl_registry_bind(registry, name,
			&xdg_wm_base_interface, 1);
		xdg_wm_base_add_listener(wl->xdg_wm_base, &xdg_wm_base_listener, NULL);

	} else if (strcmp(iface, zwp_linux_dmabuf_v1_interface.name) == 0) {
		wl->dmabuf = wl_registry_bind(registry, name,
			&zwp_linux_dmabuf_v1_interface, 3);
		zwp_linux_dmabuf_v1_add_listener(wl->dmabuf, &dmabuf_listener, wl);
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
	wlr_format_set_release(&wl->formats);
	close(wl->render_fd);

	if (wl->pointer) {
		wl_pointer_destroy(wl->pointer);
	}
	if (wl->seat) {
		wl_seat_destroy(wl->seat);
	}
	if (wl->shm) {
		wl_shm_destroy(wl->shm);
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

static int backend_get_render_fd(struct wlr_backend *backend) {
	struct wlr_wl_backend *wl = get_wl_backend_from_backend(backend);
	return wl->render_fd;
}

static bool backend_attach_gbm(struct wlr_backend *backend, struct wlr_gbm_image *img) {
	struct wlr_wl_backend *wl = get_wl_backend_from_backend(backend);
	struct gbm_bo *bo = img->bo;

	struct zwp_linux_buffer_params_v1 *params =
		zwp_linux_dmabuf_v1_create_params(wl->dmabuf);

	int fd = gbm_bo_get_fd(bo);
	uint64_t mod = gbm_bo_get_modifier(bo);

	int n = gbm_bo_get_plane_count(bo);
	for (int i = 0; i < n; ++i) {
		zwp_linux_buffer_params_v1_add(params, fd, i,
			gbm_bo_get_offset(bo, i),
			gbm_bo_get_stride_for_plane(bo, i),
			mod >> 32, mod & 0xffffffff);
	}

	struct wl_buffer *buf = zwp_linux_buffer_params_v1_create_immed(params,
		gbm_bo_get_width(bo), gbm_bo_get_height(bo),
		gbm_bo_get_format(bo), 0);

	zwp_linux_buffer_params_v1_destroy(params);

	img->base.backend_priv = buf;

	return true;
}

static void backend_detach_gbm(struct wlr_backend *backend, struct wlr_gbm_image *img) {
	wl_buffer_destroy(img->base.backend_priv);
	img->base.backend_priv = NULL;
}

static struct wlr_backend_impl backend_impl = {
	.start = backend_start,
	.destroy = backend_destroy,
	.get_renderer = backend_get_renderer,
	.get_render_fd = backend_get_render_fd,
	.attach_gbm = backend_attach_gbm,
	.detach_gbm = backend_detach_gbm,
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

	/*
	 * XXX: This is a hack until render fds are added to wp_linux_dmabuf
	 */
	wl->render_fd = open("/dev/dri/renderD128", O_RDWR | O_NONBLOCK | O_CLOEXEC);
	if (wl->render_fd < 0) {
		wlr_log_errno(WLR_ERROR, "Failed to open render node");
		goto error_renderer;
	}

	wl->local_display_destroy.notify = handle_display_destroy;
	wl_display_add_destroy_listener(display, &wl->local_display_destroy);

	return &wl->backend;

error_renderer:
	wlr_renderer_destroy(wl->renderer);
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
