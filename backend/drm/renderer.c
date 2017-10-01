#include <stdbool.h>

#include <gbm.h>
#include <GLES2/gl2.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>

#include <wlr/egl.h>
#include <wlr/util/log.h>
#include "backend/drm/drm.h"

bool wlr_drm_renderer_init(struct wlr_drm_renderer *renderer, int fd) {
	renderer->gbm = gbm_create_device(fd);
	if (!renderer->gbm) {
		wlr_log(L_ERROR, "Failed to create GBM device");
		return false;
	}

	if (!wlr_egl_init(&renderer->egl, EGL_PLATFORM_GBM_MESA,
			GBM_FORMAT_ARGB8888, renderer->gbm)) {
		gbm_device_destroy(renderer->gbm);
		return false;
	}

	renderer->fd = fd;
	return true;
}

void wlr_drm_renderer_finish(struct wlr_drm_renderer *renderer) {
	if (!renderer) {
		return;
	}

	wlr_egl_free(&renderer->egl);
	gbm_device_destroy(renderer->gbm);
}

bool wlr_drm_surface_init(struct wlr_drm_surface *surf,
		struct wlr_drm_renderer *renderer, uint32_t width, uint32_t height,
		uint32_t format, uint32_t flags) {
	if (surf->width == width && surf->height == height) {
		return true;
	}

	surf->renderer = renderer;
	surf->width = width;
	surf->height = height;

	surf->gbm = gbm_surface_create(renderer->gbm, width, height,
		format, GBM_BO_USE_RENDERING | flags);
	if (!surf->gbm) {
		wlr_log_errno(L_ERROR, "Failed to create GBM surface");
		goto error_zero;
	}

	surf->egl = wlr_egl_create_surface(&renderer->egl, surf->gbm);
	if (surf->egl == EGL_NO_SURFACE) {
		wlr_log(L_ERROR, "Failed to create EGL surface");
		goto error_gbm;
	}

	return true;

error_gbm:
	gbm_surface_destroy(surf->gbm);
error_zero:
	memset(surf, 0, sizeof(*surf));
	return false;
}

void wlr_drm_surface_finish(struct wlr_drm_surface *surf) {
	if (!surf || !surf->renderer) {
		return;
	}

	eglMakeCurrent(surf->renderer->egl.display, EGL_NO_SURFACE, EGL_NO_SURFACE,
		EGL_NO_CONTEXT);

	if (surf->front) {
		gbm_surface_release_buffer(surf->gbm, surf->front);
	}
	if (surf->back) {
		gbm_surface_release_buffer(surf->gbm, surf->back);
	}

	if (surf->egl) {
		eglDestroySurface(surf->renderer->egl.display, surf->egl);
	}
	if (surf->gbm) {
		gbm_surface_destroy(surf->gbm);
	}

	memset(surf, 0, sizeof(*surf));
}

void wlr_drm_surface_make_current(struct wlr_drm_surface *surf) {
	eglMakeCurrent(surf->renderer->egl.display, surf->egl, surf->egl,
		surf->renderer->egl.context);
}

struct gbm_bo *wlr_drm_surface_swap_buffers(struct wlr_drm_surface *surf) {
	if (surf->front) {
		gbm_surface_release_buffer(surf->gbm, surf->front);
	}

	eglSwapBuffers(surf->renderer->egl.display, surf->egl);

	surf->front = surf->back;
	surf->back = gbm_surface_lock_front_buffer(surf->gbm);
	return surf->back;
}

struct gbm_bo *wlr_drm_surface_get_front(struct wlr_drm_surface *surf) {
	if (surf->front) {
		return surf->front;
	}

	wlr_drm_surface_make_current(surf);
	glViewport(0, 0, surf->width, surf->height);
	glClearColor(0.0, 0.0, 0.0, 1.0);
	glClear(GL_COLOR_BUFFER_BIT);
	return wlr_drm_surface_swap_buffers(surf);
}
