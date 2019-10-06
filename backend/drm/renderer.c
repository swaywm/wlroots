#include <assert.h>
#include <drm_fourcc.h>
#include <gbm.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <wayland-util.h>
#include <wlr/render/egl.h>
#include <wlr/render/gles2.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_matrix.h>
#include <wlr/util/log.h>
#include "backend/drm/drm.h"
#include "glapi.h"

bool init_drm_renderer(struct wlr_drm_backend *drm,
		struct wlr_drm_renderer *renderer, wlr_renderer_create_func_t create_renderer_func) {
	renderer->gbm = gbm_create_device(drm->fd);
	if (!renderer->gbm) {
		wlr_log(WLR_ERROR, "Failed to create GBM device");
		return false;
	}

	if (!create_renderer_func) {
		create_renderer_func = wlr_renderer_autocreate;
	}

	static EGLint config_attribs[] = {
		EGL_RED_SIZE, 1,
		EGL_GREEN_SIZE, 1,
		EGL_BLUE_SIZE, 1,
		EGL_ALPHA_SIZE, 1,
		EGL_NONE,
	};

	renderer->gbm_format = GBM_FORMAT_ARGB8888;
	renderer->wlr_rend = create_renderer_func(&renderer->egl,
		EGL_PLATFORM_GBM_MESA, renderer->gbm,
		config_attribs, renderer->gbm_format);
	if (!renderer->wlr_rend) {
		wlr_log(WLR_ERROR, "Failed to create EGL/WLR renderer");
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
	wlr_egl_finish(&renderer->egl);
	gbm_device_destroy(renderer->gbm);
}

bool init_drm_surface(struct wlr_drm_surface *surf,
		struct wlr_drm_renderer *renderer, uint32_t width, uint32_t height,
		uint32_t format, struct wlr_drm_format_set *set, uint32_t flags) {
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
	wlr_egl_destroy_surface(&surf->renderer->egl, surf->egl);

	if (!(flags & GBM_BO_USE_LINEAR) && set != NULL) {
		const struct wlr_drm_format *drm_format =
			wlr_drm_format_set_get(set, format);
		if (drm_format != NULL) {
			surf->gbm = gbm_surface_create_with_modifiers(renderer->gbm,
				width, height, format, drm_format->modifiers, drm_format->len);
		}
	}

	if (surf->gbm == NULL) {
		surf->gbm = gbm_surface_create(renderer->gbm, width, height,
			format, GBM_BO_USE_RENDERING | flags);
	}
	if (!surf->gbm) {
		wlr_log_errno(WLR_ERROR, "Failed to create GBM surface");
		goto error_zero;
	}

	surf->egl = wlr_egl_create_surface(&renderer->egl, surf->gbm);
	if (surf->egl == EGL_NO_SURFACE) {
		wlr_log(WLR_ERROR, "Failed to create EGL surface");
		goto error_gbm;
	}

	return true;

error_gbm:
	gbm_surface_destroy(surf->gbm);
error_zero:
	memset(surf, 0, sizeof(*surf));
	return false;
}

void finish_drm_surface(struct wlr_drm_surface *surf) {
	if (!surf || !surf->renderer) {
		return;
	}

	if (surf->front) {
		gbm_surface_release_buffer(surf->gbm, surf->front);
	}
	if (surf->back) {
		gbm_surface_release_buffer(surf->gbm, surf->back);
	}

	wlr_egl_destroy_surface(&surf->renderer->egl, surf->egl);
	if (surf->gbm) {
		gbm_surface_destroy(surf->gbm);
	}

	memset(surf, 0, sizeof(*surf));
}

bool make_drm_surface_current(struct wlr_drm_surface *surf,
		int *buffer_damage) {
	return wlr_egl_make_current(&surf->renderer->egl, surf->egl, buffer_damage);
}

struct gbm_bo *swap_drm_surface_buffers(struct wlr_drm_surface *surf,
		pixman_region32_t *damage) {
	if (surf->front) {
		gbm_surface_release_buffer(surf->gbm, surf->front);
	}

	wlr_egl_swap_buffers(&surf->renderer->egl, surf->egl, damage);

	surf->front = surf->back;
	surf->back = gbm_surface_lock_front_buffer(surf->gbm);
	return surf->back;
}

struct gbm_bo *get_drm_surface_front(struct wlr_drm_surface *surf) {
	if (surf->front) {
		return surf->front;
	}

	make_drm_surface_current(surf, NULL);
	struct wlr_renderer *renderer = surf->renderer->wlr_rend;
	wlr_renderer_begin(renderer, surf->width, surf->height);
	wlr_renderer_clear(renderer, (float[]){ 0.0, 0.0, 0.0, 1.0 });
	wlr_renderer_end(renderer);
	return swap_drm_surface_buffers(surf, NULL);
}

void post_drm_surface(struct wlr_drm_surface *surf) {
	if (surf->front) {
		gbm_surface_release_buffer(surf->gbm, surf->front);
		surf->front = NULL;
	}
}

struct gbm_bo *import_gbm_bo(struct wlr_drm_renderer *renderer,
		struct wlr_dmabuf_attributes *attribs) {
	if (attribs->modifier == DRM_FORMAT_MOD_INVALID && attribs->n_planes == 1
			&& attribs->offset[0] == 0) {
		struct gbm_import_fd_data data = {
			.fd = attribs->fd[0],
			.width = attribs->width,
			.height = attribs->height,
			.stride = attribs->stride[0],
			.format = attribs->format,
		};

		return gbm_bo_import(renderer->gbm, GBM_BO_IMPORT_FD,
			&data, GBM_BO_USE_SCANOUT);
	} else {
		struct gbm_import_fd_modifier_data data = {
			.width = attribs->width,
			.height = attribs->height,
			.format = attribs->format,
			.num_fds = attribs->n_planes,
			.modifier = attribs->modifier,
		};

		if ((size_t)attribs->n_planes > sizeof(data.fds) / sizeof(data.fds[0])) {
			return NULL;
		}

		for (size_t i = 0; i < (size_t)attribs->n_planes; ++i) {
			data.fds[i] = attribs->fd[i];
			data.strides[i] = attribs->stride[i];
			data.offsets[i] = attribs->offset[i];
		}

		return gbm_bo_import(renderer->gbm, GBM_BO_IMPORT_FD_MODIFIER,
			&data, GBM_BO_USE_SCANOUT);
	}
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

struct gbm_bo *copy_drm_surface_mgpu(struct wlr_drm_surface *dest,
		struct gbm_bo *src) {
	make_drm_surface_current(dest, NULL);

	struct wlr_texture *tex = get_tex_for_bo(dest->renderer, src);
	assert(tex);

	float mat[9];
	wlr_matrix_projection(mat, 1, 1, WL_OUTPUT_TRANSFORM_NORMAL);

	struct wlr_renderer *renderer = dest->renderer->wlr_rend;
	wlr_renderer_begin(renderer, dest->width, dest->height);
	wlr_renderer_clear(renderer, (float[]){ 0.0, 0.0, 0.0, 0.0 });
	wlr_render_texture_with_matrix(renderer, tex, mat, 1.0f);
	wlr_renderer_end(renderer);

	return swap_drm_surface_buffers(dest, NULL);
}

bool init_drm_plane_surfaces(struct wlr_drm_plane *plane,
		struct wlr_drm_backend *drm, int32_t width, uint32_t height,
		uint32_t format) {
	if (!drm->parent) {
		return init_drm_surface(&plane->surf, &drm->renderer, width, height,
			format, &plane->formats, GBM_BO_USE_SCANOUT);
	}

	if (!init_drm_surface(&plane->surf, &drm->parent->renderer,
			width, height, format, NULL, GBM_BO_USE_LINEAR)) {
		return false;
	}

	if (!init_drm_surface(&plane->mgpu_surf, &drm->renderer,
			width, height, format, &plane->formats, GBM_BO_USE_SCANOUT)) {
		finish_drm_surface(&plane->surf);
		return false;
	}

	return true;
}
