#include <assert.h>
#include <stdlib.h>
#include <wlr/interfaces/wlr_input_device.h>
#include <wlr/interfaces/wlr_output.h>
#include <wlr/render/egl.h>
#include <wlr/render/gles2.h>
#include <wlr/util/log.h>
#include "backend/headless.h"
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

	wlr_renderer_destroy(backend->renderer);
	wlr_egl_finish(&backend->egl);
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

struct wlr_backend *wlr_headless_backend_create(struct wl_display *display,
		wlr_renderer_create_func_t create_renderer_func) {
	wlr_log(WLR_INFO, "Creating headless backend");

	struct wlr_headless_backend *backend =
		calloc(1, sizeof(struct wlr_headless_backend));
	if (!backend) {
		wlr_log(WLR_ERROR, "Failed to allocate wlr_headless_backend");
		return NULL;
	}
	wlr_backend_init(&backend->backend, &backend_impl);
	backend->display = display;
	wl_list_init(&backend->outputs);
	wl_list_init(&backend->input_devices);

	static const EGLint config_attribs[] = {
		EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
		EGL_ALPHA_SIZE, 0,
		EGL_BLUE_SIZE, 1,
		EGL_GREEN_SIZE, 1,
		EGL_RED_SIZE, 1,
		EGL_NONE,
	};

	if (!create_renderer_func) {
		create_renderer_func = wlr_renderer_autocreate;
	}

	backend->renderer = create_renderer_func(&backend->egl,
		EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY,
		(EGLint*)config_attribs, 0);
	if (!backend->renderer) {
		wlr_log(WLR_ERROR, "Failed to create renderer");
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
