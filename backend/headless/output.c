#include <stdlib.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <wlr/interfaces/wlr_output.h>
#include <wlr/util/log.h>
#include "backend/headless.h"

static void output_destroy(struct wlr_output *wlr_output) {
	struct wlr_headless_backend_output *output =
		(struct wlr_headless_backend_output *)wlr_output;
	wl_signal_emit(&output->backend->backend.events.output_remove,
		&output->wlr_output);

	wl_list_remove(&output->link);

	eglDestroySurface(output->backend->egl.display, output->egl_surface);
	free(output);
}

static const struct wlr_output_impl output_impl = {
	//.set_custom_mode = wlr_wl_output_set_custom_mode,
	//.transform = wlr_wl_output_transform,
	.destroy = output_destroy,
	//.make_current = wlr_wl_output_make_current,
	//.swap_buffers = wlr_wl_output_swap_buffers,
};

static EGLSurface egl_create_surface(struct wlr_egl *egl, unsigned int width,
		unsigned int height) {
	EGLint attribs[] = {EGL_WIDTH, width, EGL_HEIGHT, height, EGL_NONE};

	EGLSurface surf = eglCreatePbufferSurface(egl->display, egl->config, attribs);
	if (surf == EGL_NO_SURFACE) {
		wlr_log(L_ERROR, "Failed to create EGL surface: %s", egl_error());
		return EGL_NO_SURFACE;
	}
	return surf;
}

struct wlr_output *wlr_headless_add_output(struct wlr_backend *wlr_backend,
		unsigned int width, unsigned int height) {
	struct wlr_headless_backend *backend =
		(struct wlr_headless_backend *)wlr_backend;

	struct wlr_headless_backend_output *output =
		calloc(1, sizeof(struct wlr_headless_backend_output));
	if (output == NULL) {
		wlr_log(L_ERROR, "Failed to allocate wlr_headless_backend_output");
		return NULL;
	}
	wlr_output_init(&output->wlr_output, &backend->backend, &output_impl);
	struct wlr_output *wlr_output = &output->wlr_output;

	output->egl_surface = egl_create_surface(&backend->egl, width, height);

	wlr_output_update_size(wlr_output, width, height);

	if (!eglMakeCurrent(output->backend->egl.display,
			output->egl_surface, output->egl_surface,
			output->backend->egl.context)) {
		wlr_log(L_ERROR, "eglMakeCurrent failed: %s", egl_error());
		goto error;
	}

	glViewport(0, 0, wlr_output->width, wlr_output->height);
	glClearColor(1.0, 1.0, 1.0, 1.0);
	glClear(GL_COLOR_BUFFER_BIT);

	if (!eglSwapBuffers(output->backend->egl.display, output->egl_surface)) {
		wlr_log(L_ERROR, "eglSwapBuffers failed: %s", egl_error());
		goto error;
	}

	wl_list_insert(&backend->outputs, &output->link);
	wlr_output_create_global(wlr_output, backend->display);
	wl_signal_emit(&backend->backend.events.output_add, wlr_output);
	return wlr_output;

error:
	wlr_output_destroy(&output->wlr_output);
	return NULL;
}
