#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <drm_fourcc.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <wlr/interfaces/wlr_input_device.h>
#include <wlr/interfaces/wlr_output.h>
#include <wlr/render/egl.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/render/gles2.h>
#include <wlr/util/log.h>
#include <xf86drm.h>
#include "backend/headless.h"
#include "render/drm_format_set.h"
#include "render/gbm_allocator.h"
#include "render/wlr_renderer.h"
#include "util/signal.h"

struct wlr_headless_backend *headless_backend_from_backend(
		struct wlr_backend *wlr_backend) {
	assert(wlr_backend_is_headless(wlr_backend));
	return (struct wlr_headless_backend *)wlr_backend;
}

static bool backend_start(struct wlr_backend *wlr_backend) {
	struct wlr_headless_backend *backend =
		headless_backend_from_backend(wlr_backend);
	wlr_log(WLR_INFO, "Starting headless backend");

	struct wlr_headless_output *output;
	wl_list_for_each(output, &backend->outputs, link) {
		wl_event_source_timer_update(output->frame_timer, output->frame_delay);
		wlr_output_update_enabled(&output->wlr_output, true);
		wlr_signal_emit_safe(&backend->backend.events.new_output,
			&output->wlr_output);
	}

	struct wlr_headless_input_device *input_device;
	wl_list_for_each(input_device, &backend->input_devices,
			wlr_input_device.link) {
		wlr_signal_emit_safe(&backend->backend.events.new_input,
			&input_device->wlr_input_device);
	}

	backend->started = true;
	return true;
}

static void backend_destroy(struct wlr_backend *wlr_backend) {
	struct wlr_headless_backend *backend =
		headless_backend_from_backend(wlr_backend);
	if (!wlr_backend) {
		return;
	}

	wl_list_remove(&backend->display_destroy.link);
	wl_list_remove(&backend->renderer_destroy.link);

	struct wlr_headless_output *output, *output_tmp;
	wl_list_for_each_safe(output, output_tmp, &backend->outputs, link) {
		wlr_output_destroy(&output->wlr_output);
	}

	struct wlr_headless_input_device *input_device, *input_device_tmp;
	wl_list_for_each_safe(input_device, input_device_tmp,
			&backend->input_devices, wlr_input_device.link) {
		wlr_input_device_destroy(&input_device->wlr_input_device);
	}

	wlr_signal_emit_safe(&wlr_backend->events.destroy, backend);

	free(backend->format);
	if (backend->egl == &backend->priv_egl) {
		wlr_renderer_destroy(backend->renderer);
		wlr_egl_finish(&backend->priv_egl);
	}
	wlr_allocator_destroy(backend->allocator);
	free(backend);
}

static struct wlr_renderer *backend_get_renderer(
		struct wlr_backend *wlr_backend) {
	struct wlr_headless_backend *backend =
		headless_backend_from_backend(wlr_backend);
	return backend->renderer;
}

static const struct wlr_backend_impl backend_impl = {
	.start = backend_start,
	.destroy = backend_destroy,
	.get_renderer = backend_get_renderer,
};

static void handle_display_destroy(struct wl_listener *listener, void *data) {
	struct wlr_headless_backend *backend =
		wl_container_of(listener, backend, display_destroy);
	backend_destroy(&backend->backend);
}

static void handle_renderer_destroy(struct wl_listener *listener, void *data) {
	struct wlr_headless_backend *backend =
		wl_container_of(listener, backend, renderer_destroy);
	backend_destroy(&backend->backend);
}

static bool backend_init(struct wlr_headless_backend *backend,
		struct wl_display *display, struct wlr_allocator *allocator,
		struct wlr_renderer *renderer) {
	wlr_backend_init(&backend->backend, &backend_impl);
	backend->display = display;
	wl_list_init(&backend->outputs);
	wl_list_init(&backend->input_devices);

	backend->allocator = allocator;
	backend->renderer = renderer;
	backend->egl = wlr_gles2_renderer_get_egl(renderer);

	const struct wlr_drm_format_set *formats =
		wlr_renderer_get_dmabuf_render_formats(backend->renderer);
	if (formats == NULL) {
		wlr_log(WLR_ERROR, "Failed to get available DMA-BUF formats from renderer");
		return false;
	}
	const struct wlr_drm_format *format =
		wlr_drm_format_set_get(formats, DRM_FORMAT_XRGB8888);
	if (format == NULL) {
		wlr_log(WLR_ERROR, "Renderer doesn't support XRGB8888");
		return false;
	}
	backend->format = wlr_drm_format_dup(format);

	backend->display_destroy.notify = handle_display_destroy;
	wl_display_add_destroy_listener(display, &backend->display_destroy);

	wl_list_init(&backend->renderer_destroy.link);

	return true;
}

static int open_drm_render_node(void) {
	uint32_t flags = 0;
	int devices_len = drmGetDevices2(flags, NULL, 0);
	if (devices_len < 0) {
		wlr_log(WLR_ERROR, "drmGetDevices2 failed");
		return -1;
	}
	drmDevice **devices = calloc(devices_len, sizeof(drmDevice *));
	if (devices == NULL) {
		wlr_log_errno(WLR_ERROR, "Allocation failed");
		return -1;
	}
	devices_len = drmGetDevices2(flags, devices, devices_len);
	if (devices_len < 0) {
		free(devices);
		wlr_log(WLR_ERROR, "drmGetDevices2 failed");
		return -1;
	}

	int fd = -1;
	for (int i = 0; i < devices_len; i++) {
		drmDevice *dev = devices[i];
		if (dev->available_nodes & (1 << DRM_NODE_RENDER)) {
			const char *name = dev->nodes[DRM_NODE_RENDER];
			wlr_log(WLR_DEBUG, "Opening DRM render node '%s'", name);
			fd = open(name, O_RDWR | O_CLOEXEC);
			if (fd < 0) {
				wlr_log_errno(WLR_ERROR, "Failed to open '%s'", name);
				goto out;
			}
			break;
		}
	}
	if (fd < 0) {
		wlr_log(WLR_ERROR, "Failed to find any DRM render node");
	}

out:
	for (int i = 0; i < devices_len; i++) {
		drmFreeDevice(&devices[i]);
	}
	free(devices);

	return fd;
}

struct wlr_backend *wlr_headless_backend_create(struct wl_display *display) {
	wlr_log(WLR_INFO, "Creating headless backend");

	int drm_fd = open_drm_render_node();
	if (drm_fd < 0) {
		wlr_log(WLR_ERROR, "Failed to open DRM render node");
		return NULL;
	}

	struct wlr_gbm_allocator *gbm_alloc = wlr_gbm_allocator_create(drm_fd);
	if (gbm_alloc == NULL) {
		wlr_log(WLR_ERROR, "Failed to create GBM allocator");
		return false;
	}

	struct wlr_headless_backend *backend =
		calloc(1, sizeof(struct wlr_headless_backend));
	if (!backend) {
		wlr_log(WLR_ERROR, "Failed to allocate wlr_headless_backend");
		goto error_backend;
	}

	struct wlr_renderer *renderer = wlr_renderer_autocreate(&backend->priv_egl,
		EGL_PLATFORM_GBM_KHR, gbm_alloc->gbm_device);
	if (!renderer) {
		wlr_log(WLR_ERROR, "Failed to create renderer");
		goto error_renderer;
	}

	if (!backend_init(backend, display, &gbm_alloc->base, renderer)) {
		goto error_init;
	}

	return &backend->backend;

error_init:
	wlr_renderer_destroy(renderer);
error_renderer:
	free(backend);
error_backend:
	wlr_allocator_destroy(&gbm_alloc->base);
	return NULL;
}

struct wlr_backend *wlr_headless_backend_create_with_renderer(
		struct wl_display *display, struct wlr_renderer *renderer) {
	wlr_log(WLR_INFO, "Creating headless backend with parent renderer");

	int drm_fd = wlr_renderer_get_drm_fd(renderer);
	if (drm_fd < 0) {
		wlr_log(WLR_ERROR, "Failed to get DRM device FD from renderer");
		return false;
	}

	drm_fd = fcntl(drm_fd, F_DUPFD_CLOEXEC, 0);
	if (drm_fd < 0) {
		wlr_log_errno(WLR_ERROR, "fcntl(F_DUPFD_CLOEXEC) failed");
		return false;
	}

	struct wlr_gbm_allocator *gbm_alloc = wlr_gbm_allocator_create(drm_fd);
	if (gbm_alloc == NULL) {
		wlr_log(WLR_ERROR, "Failed to create GBM allocator");
		return false;
	}

	struct wlr_headless_backend *backend =
		calloc(1, sizeof(struct wlr_headless_backend));
	if (!backend) {
		wlr_log(WLR_ERROR, "Failed to allocate wlr_headless_backend");
		goto error_backend;
	}

	if (!backend_init(backend, display, &gbm_alloc->base, renderer)) {
		goto error_init;
	}

	backend->renderer_destroy.notify = handle_renderer_destroy;
	wl_signal_add(&renderer->events.destroy, &backend->renderer_destroy);

	return &backend->backend;

error_init:
	free(backend);
error_backend:
	wlr_allocator_destroy(&gbm_alloc->base);
	return NULL;
}

bool wlr_backend_is_headless(struct wlr_backend *backend) {
	return backend->impl == &backend_impl;
}
