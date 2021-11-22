#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <drm_fourcc.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <wlr/interfaces/wlr_input_device.h>
#include <wlr/interfaces/wlr_output.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/util/log.h>
#include <xf86drm.h>
#include "backend/headless.h"
#include "render/drm_format_set.h"
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
	wl_list_for_each(input_device, &backend->input_devices, link) {
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

	struct wlr_headless_output *output, *output_tmp;
	wl_list_for_each_safe(output, output_tmp, &backend->outputs, link) {
		wlr_output_destroy(&output->wlr_output);
	}

	struct wlr_headless_input_device *input_device, *input_device_tmp;
	wl_list_for_each_safe(input_device, input_device_tmp,
			&backend->input_devices, link) {
		wlr_input_device_destroy(&input_device->wlr_input_device);
	}

	wlr_backend_finish(wlr_backend);

	close(backend->drm_fd);
	free(backend);
}

static int backend_get_drm_fd(struct wlr_backend *wlr_backend) {
	struct wlr_headless_backend *backend =
		headless_backend_from_backend(wlr_backend);
	return backend->drm_fd;
}

static uint32_t get_buffer_caps(struct wlr_backend *wlr_backend) {
	return WLR_BUFFER_CAP_DATA_PTR
		| WLR_BUFFER_CAP_DMABUF
		| WLR_BUFFER_CAP_SHM;
}

static const struct wlr_backend_impl backend_impl = {
	.start = backend_start,
	.destroy = backend_destroy,
	.get_drm_fd = backend_get_drm_fd,
	.get_buffer_caps = get_buffer_caps,
};

static void handle_display_destroy(struct wl_listener *listener, void *data) {
	struct wlr_headless_backend *backend =
		wl_container_of(listener, backend, display_destroy);
	backend_destroy(&backend->backend);
}

static bool backend_init(struct wlr_headless_backend *backend,
		struct wl_display *display) {
	wlr_backend_init(&backend->backend, &backend_impl);

	backend->display = display;
	wl_list_init(&backend->outputs);
	wl_list_init(&backend->input_devices);

	backend->display_destroy.notify = handle_display_destroy;
	wl_display_add_destroy_listener(display, &backend->display_destroy);

	return true;
}

static int open_drm_render_node(void) {
	uint32_t flags = 0;
	int devices_len = drmGetDevices2(flags, NULL, 0);
	if (devices_len < 0) {
		wlr_log(WLR_ERROR, "drmGetDevices2 failed: %s", strerror(-devices_len));
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
		wlr_log(WLR_ERROR, "drmGetDevices2 failed: %s", strerror(-devices_len));
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

	struct wlr_headless_backend *backend =
		calloc(1, sizeof(struct wlr_headless_backend));
	if (!backend) {
		wlr_log(WLR_ERROR, "Failed to allocate wlr_headless_backend");
		return NULL;
	}

	backend->drm_fd = open_drm_render_node();
	if (backend->drm_fd < 0) {
		wlr_log(WLR_ERROR, "Failed to open DRM render node");
	}

	if (!backend_init(backend, display)) {
		goto error_init;
	}

	return &backend->backend;

error_init:
	close(backend->drm_fd);
	free(backend);
	return NULL;
}

struct wlr_backend *wlr_headless_backend_create_with_renderer(
		struct wl_display *display, struct wlr_renderer *renderer) {
	wlr_log(WLR_INFO, "Creating headless backend with parent renderer");

	struct wlr_headless_backend *backend =
		calloc(1, sizeof(struct wlr_headless_backend));
	if (!backend) {
		wlr_log(WLR_ERROR, "Failed to allocate wlr_headless_backend");
		return NULL;
	}

	int drm_fd = wlr_renderer_get_drm_fd(renderer);
	if (drm_fd < 0) {
		wlr_log(WLR_ERROR, "Failed to get DRM device FD from parent renderer");
		backend->drm_fd = -1;
	} else {
		backend->drm_fd = fcntl(drm_fd, F_DUPFD_CLOEXEC, 0);
		if (backend->drm_fd < 0) {
			wlr_log_errno(WLR_ERROR, "fcntl(F_DUPFD_CLOEXEC) failed");
		}
	}

	if (!backend_init(backend, display)) {
		goto error_init;
	}

	return &backend->backend;

error_init:
	close(backend->drm_fd);
	free(backend);
	return NULL;
}

bool wlr_backend_is_headless(struct wlr_backend *backend) {
	return backend->impl == &backend_impl;
}
