#include <assert.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <gbm.h>
#include <GLES2/gl2.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <wayland-util.h>
#include <wlr/render.h>
#include <wlr/render/egl.h>
#include <wlr/render/gles2.h>
#include <wlr/types/wlr_matrix.h>
#include <wlr/util/log.h>
#include "backend/drm/drm.h"
#include "glapi.h"

bool wlr_drm_renderer_init(struct wlr_drm_backend *drm,
		struct wlr_drm_renderer *renderer) {
	renderer->gbm = gbm_create_device(drm->fd);
	if (!renderer->gbm) {
		wlr_log(L_ERROR, "Failed to create GBM device");
		return false;
	}

	if (!wlr_egl_init(&renderer->egl, EGL_PLATFORM_GBM_MESA, renderer->gbm,
			NULL, GBM_FORMAT_ARGB8888)) {
		goto error_gbm;
	}

	renderer->wlr_rend = wlr_gles2_renderer_create(&drm->backend);
	if (!renderer->wlr_rend) {
		wlr_log(L_ERROR, "Failed to create WLR renderer");
		goto error_egl;
	}

	renderer->fd = drm->fd;
	return true;

error_egl:
	wlr_egl_finish(&renderer->egl);
error_gbm:
	gbm_device_destroy(renderer->gbm);
	return false;
}

void wlr_drm_renderer_finish(struct wlr_drm_renderer *renderer) {
	if (!renderer) {
		return;
	}

	wlr_renderer_destroy(renderer->wlr_rend);
	wlr_egl_finish(&renderer->egl);
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

	if (surf->gbm) {
		if (surf->front) {
			gbm_surface_release_buffer(surf->gbm, surf->front);
			surf->front = NULL;
		}
		if (surf->back) {
			gbm_surface_release_buffer(surf->gbm, surf->back);
			surf->back = NULL;
		}
		gbm_surface_destroy(surf->gbm);
	}
	if (surf->egl) {
		eglDestroySurface(surf->renderer->egl.display, surf->egl);
	}

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

bool wlr_drm_surface_make_current(struct wlr_drm_surface *surf,
		int *buffer_damage) {
	return wlr_egl_make_current(&surf->renderer->egl, surf->egl, buffer_damage);
}

struct gbm_bo *wlr_drm_surface_swap_buffers(struct wlr_drm_surface *surf,
		pixman_region32_t *damage) {
	if (surf->front) {
		gbm_surface_release_buffer(surf->gbm, surf->front);
	}

	wlr_egl_swap_buffers(&surf->renderer->egl, surf->egl, damage);

	surf->front = surf->back;
	surf->back = gbm_surface_lock_front_buffer(surf->gbm);
	return surf->back;
}

struct gbm_bo *wlr_drm_surface_get_front(struct wlr_drm_surface *surf) {
	if (surf->front) {
		return surf->front;
	}

	wlr_drm_surface_make_current(surf, NULL);
	glViewport(0, 0, surf->width, surf->height);
	glClearColor(0.0, 0.0, 0.0, 1.0);
	glClear(GL_COLOR_BUFFER_BIT);
	return wlr_drm_surface_swap_buffers(surf, NULL);
}

void wlr_drm_surface_post(struct wlr_drm_surface *surf) {
	if (surf->front) {
		gbm_surface_release_buffer(surf->gbm, surf->front);
		surf->front = NULL;
	}
}

struct tex {
	struct wlr_egl *egl;
	EGLImageKHR img;
	struct wlr_texture *tex;
};

static void free_eglimage(struct gbm_bo *bo, void *data) {
	struct tex *tex = data;

	wlr_egl_destroy_image(tex->egl, tex->img);
	wlr_texture_destroy(tex->tex);
	free(tex);
}

static struct wlr_texture *get_tex_for_bo(struct wlr_drm_renderer *renderer, struct gbm_bo *bo) {
	struct tex *tex = gbm_bo_get_user_data(bo);
	if (tex) {
		return tex->tex;
	}

	tex = malloc(sizeof(*tex));
	if (!tex) {
		wlr_log_errno(L_ERROR, "Allocation failed");
		return NULL;
	}

	tex->egl = &renderer->egl;

	int dmabuf_fd = gbm_bo_get_fd(bo);
	uint32_t width = gbm_bo_get_width(bo);
	uint32_t height = gbm_bo_get_height(bo);

	EGLint attribs[] = {
		EGL_WIDTH, width,
		EGL_HEIGHT, height,
		EGL_LINUX_DRM_FOURCC_EXT, gbm_bo_get_format(bo),
		EGL_DMA_BUF_PLANE0_FD_EXT, dmabuf_fd,
		EGL_DMA_BUF_PLANE0_OFFSET_EXT, gbm_bo_get_offset(bo, 0),
		EGL_DMA_BUF_PLANE0_PITCH_EXT, gbm_bo_get_stride_for_plane(bo, 0),
		EGL_IMAGE_PRESERVED_KHR, EGL_FALSE,
		EGL_NONE,
	};

	tex->img = eglCreateImageKHR(renderer->egl.display, EGL_NO_CONTEXT,
		EGL_LINUX_DMA_BUF_EXT, NULL, attribs);
	if (!tex->img) {
		wlr_log(L_ERROR, "Failed to create EGL image: %s", egl_error());
		abort();
	}

	tex->tex = wlr_render_texture_create(renderer->wlr_rend);
	wlr_texture_upload_eglimage(tex->tex, tex->img, width, height);

	gbm_bo_set_user_data(bo, tex, free_eglimage);

	return tex->tex;
}

struct gbm_bo *wlr_drm_surface_mgpu_copy(struct wlr_drm_surface *dest,
		struct gbm_bo *src) {
	wlr_drm_surface_make_current(dest, NULL);

	struct wlr_texture *tex = get_tex_for_bo(dest->renderer, src);
	assert(tex);

	static const float color[] = {0.0, 0.0, 0.0, 1.0};

	static const float mat[9] = {
		[0] = 2.0f,
		[2] = -1.0f,
		[4] = 2.0f,
		[5] = -1.0f,
		[8] = 1.0f,
	};

	glViewport(0, 0, dest->width, dest->height);
	wlr_renderer_clear(dest->renderer->wlr_rend, color);
	wlr_render_texture_with_matrix(dest->renderer->wlr_rend, tex, mat, 1.0f);

	return wlr_drm_surface_swap_buffers(dest, NULL);
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
