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
		wlr_output_create_global(&output->wlr_output, backend->display);
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

static bool egl_get_config(EGLDisplay disp, EGLConfig *out) {
	EGLint count = 0, matched = 0, ret;

	ret = eglGetConfigs(disp, NULL, 0, &count);
	if (ret == EGL_FALSE || count == 0) {
		wlr_log(L_ERROR, "eglGetConfigs returned no configs");
		return false;
	}

	EGLConfig configs[count];

	static const EGLint attribs[] = {
		EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
		EGL_ALPHA_SIZE, 0,
		EGL_BLUE_SIZE, 8,
		EGL_GREEN_SIZE, 8,
		EGL_RED_SIZE, 8,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
		EGL_NONE,
	};

	ret = eglChooseConfig(disp, attribs, configs, count, &matched);
	if (ret == EGL_FALSE) {
		wlr_log(L_ERROR, "eglChooseConfig failed");
		return false;
	}

	// TODO
	*out = configs[0];
	return true;
}

static bool egl_init(struct wlr_egl *egl) {
	if (!load_glapi()) {
		return false;
	}

	if (eglBindAPI(EGL_OPENGL_ES_API) == EGL_FALSE) {
		wlr_log(L_ERROR, "Failed to bind to the OpenGL ES API: %s", egl_error());
		goto error;
	}

	egl->display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	if (egl->display == EGL_NO_DISPLAY) {
		wlr_log(L_ERROR, "Failed to create EGL display: %s", egl_error());
		goto error;
	}

	EGLint major, minor;
	if (eglInitialize(egl->display, &major, &minor) == EGL_FALSE) {
		wlr_log(L_ERROR, "Failed to initialize EGL: %s", egl_error());
		goto error;
	}

	if (!egl_get_config(egl->display, &egl->config)) {
		wlr_log(L_ERROR, "Failed to get EGL config");
		goto error;
	}

	static const EGLint attribs[] = {
		EGL_CONTEXT_CLIENT_VERSION, 2,
		EGL_NONE,
	};

	egl->context = eglCreateContext(egl->display, egl->config,
		EGL_NO_CONTEXT, attribs);
	if (egl->context == EGL_NO_CONTEXT) {
		wlr_log(L_ERROR, "Failed to create EGL context: %s", egl_error());
		goto error;
	}

	eglMakeCurrent(egl->display, EGL_NO_SURFACE, EGL_NO_SURFACE, egl->context);
	egl->egl_exts = eglQueryString(egl->display, EGL_EXTENSIONS);
	if (strstr(egl->egl_exts, "EGL_KHR_image_base") == NULL) {
		wlr_log(L_ERROR, "Required egl extensions not supported");
		goto error;
	}

	egl->gl_exts = (const char*) glGetString(GL_EXTENSIONS);
	wlr_log(L_INFO, "Using EGL %d.%d", (int)major, (int)minor);
	wlr_log(L_INFO, "Using %s", glGetString(GL_VERSION));
	wlr_log(L_INFO, "Supported EGL extensions: %s", egl->egl_exts);
	wlr_log(L_INFO, "Supported OpenGL ES extensions: %s", egl->gl_exts);
	return true;

error:
	eglMakeCurrent(EGL_NO_DISPLAY, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
	eglTerminate(egl->display);
	eglReleaseThread();
	return false;
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

	egl_init(&backend->egl);

	backend->display_destroy.notify = handle_display_destroy;
	wl_display_add_destroy_listener(display, &backend->display_destroy);

	return &backend->backend;
}

bool wlr_backend_is_headless(struct wlr_backend *backend) {
	return backend->impl == &backend_impl;
}
