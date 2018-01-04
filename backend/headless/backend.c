#include <stdlib.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <wlr/util/log.h>
#include <wlr/interfaces/wlr_output.h>
#include <wlr/interfaces/wlr_input_device.h>
#include "backend/headless.h"
#include "glapi.h"

static bool backend_start(struct wlr_backend *wlr_backend) {
	struct wlr_headless_backend *backend =
		(struct wlr_headless_backend *)wlr_backend;
	wlr_log(L_INFO, "Starting headless backend");

	struct wlr_headless_output *output;
	wl_list_for_each(output, &backend->outputs, link) {
		wl_event_source_timer_update(output->frame_timer, output->frame_delay);
		wlr_output_update_enabled(&output->wlr_output, true);
		wl_signal_emit(&backend->backend.events.output_add,
			&output->wlr_output);
	}

	struct wlr_headless_input_device *input_device;
	wl_list_for_each(input_device, &backend->input_devices,
			wlr_input_device.link) {
		wl_signal_emit(&backend->backend.events.input_add,
			&input_device->wlr_input_device);
	}

	backend->started = true;
	return true;
}

static void backend_destroy(struct wlr_backend *wlr_backend) {
	struct wlr_headless_backend *backend =
		(struct wlr_headless_backend *)wlr_backend;
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
			&backend->input_devices, wlr_input_device.link) {
		wlr_input_device_destroy(&input_device->wlr_input_device);
	}

	wlr_egl_finish(&backend->egl);
	free(backend);
}

static struct wlr_egl *backend_get_egl(struct wlr_backend *wlr_backend) {
	struct wlr_headless_backend *backend =
		(struct wlr_headless_backend *)wlr_backend;
	return &backend->egl;
}

static const struct wlr_backend_impl backend_impl = {
	.start = backend_start,
	.destroy = backend_destroy,
	.get_egl = backend_get_egl,
};

static void handle_display_destroy(struct wl_listener *listener, void *data) {
	struct wlr_headless_backend *backend =
		wl_container_of(listener, backend, display_destroy);
	backend_destroy(&backend->backend);
}

struct wlr_backend *wlr_headless_backend_create(struct wl_display *display) {
	wlr_log(L_INFO, "Creating headless backend");

	struct wlr_headless_backend *backend =
		calloc(1, sizeof(struct wlr_headless_backend));
	if (!backend) {
		wlr_log(L_ERROR, "Failed to allocate wlr_headless_backend");
		return NULL;
	}
	wlr_backend_init(&backend->backend, &backend_impl);
	backend->display = display;
	wl_list_init(&backend->outputs);
	wl_list_init(&backend->input_devices);

	static const EGLint config_attribs[] = {
		EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
		EGL_ALPHA_SIZE, 0,
		EGL_BLUE_SIZE, 8,
		EGL_GREEN_SIZE, 8,
		EGL_RED_SIZE, 8,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
		EGL_NONE,
	};
	bool ok = wlr_egl_init(&backend->egl, EGL_PLATFORM_SURFACELESS_MESA, NULL,
		(EGLint *)config_attribs, 0);
	if (!ok) {
		free(backend);
		return NULL;
	}

	backend->display_destroy.notify = handle_display_destroy;
	wl_display_add_destroy_listener(display, &backend->display_destroy);

	return &backend->backend;
}

bool wlr_backend_is_headless(struct wlr_backend *backend) {
	return backend->impl == &backend_impl;
}
