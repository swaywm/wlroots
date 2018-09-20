#include <assert.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <gbm.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <wayland-util.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_matrix.h>
#include <wlr/util/log.h>
#include "backend/drm/drm.h"

#ifndef DRM_FORMAT_MOD_LINEAR
#define DRM_FORMAT_MOD_LINEAR 0
#endif

bool init_drm_renderer(struct wlr_drm_backend *drm,
		struct wlr_drm_renderer *renderer,
		wlr_renderer_create_func_t create_renderer_func) {
	renderer->gbm = gbm_create_device(drm->fd);
	if (!renderer->gbm) {
		wlr_log(WLR_ERROR, "Failed to create GBM device");
		return false;
	}

	if (!create_renderer_func) {
		create_renderer_func = wlr_renderer_autocreate;
	}

	renderer->wlr_rend = create_renderer_func(&drm->backend);
	if (!renderer->wlr_rend) {
		wlr_log(WLR_ERROR, "Failed to create renderer");
		goto error_gbm;
	}

	renderer->fd = drm->fd;
	return true;

error_gbm:
	gbm_device_destroy(renderer->gbm);
	return false;
}

void finish_drm_renderer(struct wlr_drm_renderer *renderer) {
	if (!renderer) {
		return;
	}

	wlr_renderer_destroy(renderer->wlr_rend);
	gbm_device_destroy(renderer->gbm);
}

bool init_drm_render_surface(struct wlr_render_surface **surf,
		struct wlr_drm_renderer *renderer, uint32_t width, uint32_t height,
		uint32_t flags) {
	if (*surf) {
		wlr_render_surface_resize(*surf, width, height);
	}

	*surf = wlr_render_surface_create_gbm(
		renderer->wlr_rend, width, height, renderer->gbm, flags);
	return *surf;
}

struct gbm_bo *get_drm_render_surface_front(struct wlr_renderer *renderer,
		struct wlr_render_surface *surf) {
	struct gbm_bo *front = wlr_render_surface_get_bo(surf);
	if (front) {
		return front;
	}

	wlr_renderer_begin(renderer, surf);
	wlr_renderer_clear(renderer, (float[]){ 0.0, 0.0, 0.0, 1.0 });
	wlr_renderer_end(renderer);

	wlr_render_surface_swap_buffers(surf, NULL);
	return wlr_render_surface_get_bo(surf);
}

bool export_drm_bo(struct gbm_bo *bo, struct wlr_dmabuf_attributes *attribs) {
	memset(attribs, 0, sizeof(struct wlr_dmabuf_attributes));

	attribs->n_planes = gbm_bo_get_plane_count(bo);
	if (attribs->n_planes > WLR_DMABUF_MAX_PLANES) {
		return false;
	}

	attribs->width = gbm_bo_get_width(bo);
	attribs->height = gbm_bo_get_height(bo);
	attribs->format = gbm_bo_get_format(bo);
	attribs->modifier = gbm_bo_get_modifier(bo);

	for (int i = 0; i < attribs->n_planes; ++i) {
		attribs->offset[i] = gbm_bo_get_offset(bo, i);
		attribs->stride[i] = gbm_bo_get_stride_for_plane(bo, i);
		attribs->fd[i] = gbm_bo_get_fd(bo);
		if (attribs->fd[i] < 0) {
			for (int j = 0; j < i; ++j) {
				close(attribs->fd[j]);
			}
			return false;
		}
	}

	return true;
}

static void free_tex(struct gbm_bo *bo, void *data) {
	struct wlr_texture *tex = data;
	wlr_texture_destroy(tex);
}

static struct wlr_texture *get_tex_for_bo(struct wlr_drm_renderer *renderer,
		struct gbm_bo *bo) {
	struct wlr_texture *tex = gbm_bo_get_user_data(bo);
	if (tex) {
		return tex;
	}

	struct wlr_dmabuf_attributes attribs;
	if (!export_drm_bo(bo, &attribs)) {
		return NULL;
	}

	tex = wlr_texture_from_dmabuf(renderer->wlr_rend, &attribs);
	if (tex) {
		gbm_bo_set_user_data(bo, tex, free_tex);
	}

	return tex;
}

struct gbm_bo *copy_drm_surface_mgpu(struct wlr_drm_renderer *renderer,
		struct wlr_render_surface *dest, struct gbm_bo *src) {
	struct wlr_texture *tex = get_tex_for_bo(renderer, src);
	assert(tex);

	float mat[9];
	wlr_matrix_projection(mat, 1, 1, WL_OUTPUT_TRANSFORM_NORMAL);

	wlr_renderer_begin(renderer->wlr_rend, dest);
	wlr_renderer_clear(renderer->wlr_rend, (float[]){ 0.0, 0.0, 0.0, 1.0 });
	wlr_render_texture_with_matrix(renderer->wlr_rend, tex, mat, 1.0f);
	wlr_renderer_end(renderer->wlr_rend);

	wlr_render_surface_swap_buffers(dest, NULL);
	return wlr_render_surface_get_bo(dest);
}

bool init_drm_plane_surfaces(struct wlr_drm_plane *plane,
		struct wlr_drm_backend *drm, int32_t width, uint32_t height) {
	if (!drm->parent) {
		return init_drm_render_surface(&plane->surf, &drm->renderer,
			width, height, GBM_BO_USE_SCANOUT);
	}

	if (!init_drm_render_surface(&plane->surf, &drm->parent->renderer,
			width, height, GBM_BO_USE_LINEAR)) {
		return false;
	}

	if (!init_drm_render_surface(&plane->mgpu_surf, &drm->renderer,
			width, height, GBM_BO_USE_SCANOUT)) {
		return false;
	}

	return true;
}
