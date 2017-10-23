#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>

#include <gbm.h>
#include <GLES2/gl2.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <wayland-server.h>
#include <wayland-util.h>

#include <wlr/util/log.h>
#include <wlr/render/egl.h>
#include <wlr/render/matrix.h>
#include <wlr/render/gles2.h>
#include <wlr/render.h>
#include "backend/drm/drm.h"
#include "render/glapi.h"

bool wlr_drm_renderer_init(struct wlr_drm_backend *drm,
		struct wlr_drm_renderer *renderer) {
	renderer->gbm = gbm_create_device(drm->fd);
	if (!renderer->gbm) {
		wlr_log(L_ERROR, "Failed to create GBM device");
		return false;
	}

	if (!wlr_egl_init(&renderer->egl, EGL_PLATFORM_GBM_MESA,
			GBM_FORMAT_ARGB8888, renderer->gbm)) {
		goto error_gbm;
	}

	renderer->rend = wlr_render_create(&drm->backend);
	if (!renderer->rend) {
		wlr_log(L_ERROR, "Failed to create WLR renderer");
		goto error_egl;
	}

	renderer->fd = drm->fd;
	return true;

error_egl:
	wlr_egl_free(&renderer->egl);
error_gbm:
	gbm_device_destroy(renderer->gbm);
	return false;
}

void wlr_drm_renderer_finish(struct wlr_drm_renderer *renderer) {
	if (!renderer) {
		return;
	}

	wlr_render_destroy(renderer->rend);
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

void wlr_drm_surface_post(struct wlr_drm_surface *surf) {
	if (surf->front) {
		gbm_surface_release_buffer(surf->gbm, surf->front);
		surf->front = NULL;
	}
}

static void free_tex(struct gbm_bo *bo, void *data) {
	wlr_tex_destroy(data);
}

static struct wlr_tex *get_tex_for_bo(struct wlr_render *rend, struct gbm_bo *bo) {
	struct wlr_tex *tex = gbm_bo_get_user_data(bo);
	if (tex) {
		return tex;
	}

	tex = wlr_tex_from_dmabuf(rend, gbm_bo_get_format(bo), gbm_bo_get_width(bo),
		gbm_bo_get_height(bo), gbm_bo_get_fd(bo), 0, gbm_bo_get_stride(bo));

	gbm_bo_set_user_data(bo, tex, free_tex);

	return tex;
}

struct gbm_bo *wlr_drm_surface_mgpu_copy(struct wlr_drm_surface *dest, struct gbm_bo *src) {
	wlr_drm_surface_make_current(dest);

	struct wlr_render *rend = dest->renderer->rend;
	struct wlr_tex *tex = get_tex_for_bo(rend, src);

	// TODO: Handle this error properly
	if (!tex) {
		abort();
	}

	wlr_render_bind_raw(rend, dest->width, dest->height, WL_OUTPUT_TRANSFORM_NORMAL);
	wlr_render_clear(rend, 0.0, 0.0, 0.0, 1.0);
	wlr_render_texture(rend, tex, 0, 0, dest->width, dest->height, 0);

	return wlr_drm_surface_swap_buffers(dest);
}

bool wlr_drm_plane_surfaces_init(struct wlr_drm_plane *plane, struct wlr_drm_backend *drm,
		int32_t width, uint32_t height, uint32_t format) {
	if (!drm->parent) {
		return wlr_drm_surface_init(&plane->surf, &drm->renderer, width, height,
			format, GBM_BO_USE_SCANOUT);
	}

	if (!wlr_drm_surface_init(&plane->surf, &drm->parent->renderer,
			width, height, format, GBM_BO_USE_LINEAR)) {
		return false;
	}

	if (!wlr_drm_surface_init(&plane->mgpu_surf, &drm->renderer,
			width, height, format, GBM_BO_USE_SCANOUT)) {
		wlr_drm_surface_finish(&plane->surf);
		return false;
	}

	return true;
}
