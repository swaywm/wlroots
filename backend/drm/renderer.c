#include <assert.h>
#include <drm_fourcc.h>
#include <gbm.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wayland-util.h>
#include <wlr/render/egl.h>
#include <wlr/render/gles2.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_matrix.h>
#include <wlr/util/log.h>
#include "backend/drm/drm.h"
#include "render/drm_format_set.h"
#include "render/gbm_allocator.h"
#include "render/swapchain.h"
#include "render/wlr_renderer.h"

bool init_drm_renderer(struct wlr_drm_backend *drm,
		struct wlr_drm_renderer *renderer, wlr_renderer_create_func_t create_renderer_func) {
	// TODO: get rid of renderer->gbm
	renderer->gbm = gbm_create_device(drm->fd);
	if (!renderer->gbm) {
		wlr_log(WLR_ERROR, "Failed to create GBM device");
		return false;
	}

	if (!create_renderer_func) {
		create_renderer_func = wlr_renderer_autocreate;
	}

	static EGLint config_attribs[] = {
		EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
		EGL_RED_SIZE, 1,
		EGL_GREEN_SIZE, 1,
		EGL_BLUE_SIZE, 1,
		EGL_ALPHA_SIZE, 1,
		EGL_NONE,
	};

	renderer->gbm_format = GBM_FORMAT_ARGB8888;
	renderer->wlr_rend = create_renderer_func(&renderer->egl,
		EGL_PLATFORM_GBM_KHR, renderer->gbm, config_attribs,
		renderer->gbm_format);
	if (!renderer->wlr_rend) {
		wlr_log(WLR_ERROR, "Failed to create EGL/WLR renderer");
		goto error_gbm;
	}

	renderer->allocator = wlr_gbm_allocator_create(drm->fd);
	if (renderer->allocator == NULL) {
		wlr_log(WLR_ERROR, "Failed to create allocator");
		goto error_wlr_rend;
	}

	renderer->fd = drm->fd;
	return true;

error_wlr_rend:
	wlr_renderer_destroy(renderer->wlr_rend);
error_gbm:
	gbm_device_destroy(renderer->gbm);
	return false;
}

void finish_drm_renderer(struct wlr_drm_renderer *renderer) {
	if (!renderer) {
		return;
	}

	wlr_allocator_destroy(&renderer->allocator->base);
	wlr_renderer_destroy(renderer->wlr_rend);
	wlr_egl_finish(&renderer->egl);
	gbm_device_destroy(renderer->gbm);
}

static bool init_drm_surface(struct wlr_drm_surface *surf,
		struct wlr_drm_renderer *renderer, uint32_t width, uint32_t height,
		const struct wlr_drm_format *drm_format, uint32_t flags) {
	if (surf->width == width && surf->height == height) {
		return true;
	}

	surf->renderer = renderer;
	surf->width = width;
	surf->height = height;

	wlr_buffer_unlock(surf->back_buffer);
	surf->back_buffer = NULL;
	wlr_swapchain_destroy(surf->swapchain);
	surf->swapchain = NULL;

	struct wlr_drm_format *format_linear = NULL;
	if (flags & GBM_BO_USE_LINEAR) {
		format_linear = calloc(1, sizeof(struct wlr_drm_format) + sizeof(uint64_t));
		if (format_linear == NULL) {
			return false;
		}
		format_linear->format = drm_format->format;
		format_linear->len = 1;
		format_linear->cap = 1;
		format_linear->modifiers[0] = DRM_FORMAT_MOD_LINEAR;
		drm_format = format_linear;
	}

	surf->swapchain = wlr_swapchain_create(&renderer->allocator->base,
		width, height, drm_format);
	free(format_linear);
	if (surf->swapchain == NULL) {
		wlr_log(WLR_ERROR, "Failed to create swapchain");
		memset(surf, 0, sizeof(*surf));
		return false;
	}

	return true;
}

static void finish_drm_surface(struct wlr_drm_surface *surf) {
	if (!surf || !surf->renderer) {
		return;
	}

	wlr_buffer_unlock(surf->back_buffer);
	wlr_swapchain_destroy(surf->swapchain);

	memset(surf, 0, sizeof(*surf));
}

bool drm_surface_make_current(struct wlr_drm_surface *surf,
		int *buffer_age) {
	wlr_buffer_unlock(surf->back_buffer);
	surf->back_buffer = wlr_swapchain_acquire(surf->swapchain, buffer_age);
	if (surf->back_buffer == NULL) {
		wlr_log(WLR_ERROR, "Failed to acquire swapchain buffer");
		return false;
	}

	if (!wlr_egl_make_current(&surf->renderer->egl, EGL_NO_SURFACE, NULL)) {
		return false;
	}
	if (!wlr_renderer_bind_buffer(surf->renderer->wlr_rend, surf->back_buffer)) {
		wlr_log(WLR_ERROR, "Failed to attach buffer to renderer");
		return false;
	}

	return true;
}

void drm_surface_unset_current(struct wlr_drm_surface *surf) {
	assert(surf->back_buffer != NULL);

	wlr_renderer_bind_buffer(surf->renderer->wlr_rend, NULL);
	wlr_egl_unset_current(&surf->renderer->egl);

	wlr_buffer_unlock(surf->back_buffer);
	surf->back_buffer = NULL;
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

	wlr_dmabuf_attributes_finish(&attribs);

	return tex;
}

void drm_plane_finish_surface(struct wlr_drm_plane *plane) {
	if (!plane) {
		return;
	}

	drm_fb_clear(&plane->pending_fb);
	drm_fb_clear(&plane->queued_fb);
	drm_fb_clear(&plane->current_fb);

	finish_drm_surface(&plane->surf);
	finish_drm_surface(&plane->mgpu_surf);
}

static uint32_t strip_alpha_channel(uint32_t format) {
	switch (format) {
	case DRM_FORMAT_ARGB8888:
		return DRM_FORMAT_XRGB8888;
	default:
		return DRM_FORMAT_INVALID;
	}
}

bool drm_plane_init_surface(struct wlr_drm_plane *plane,
		struct wlr_drm_backend *drm, int32_t width, uint32_t height,
		uint32_t format, uint32_t flags, bool with_modifiers) {
	if (!wlr_drm_format_set_has(&plane->formats, format, DRM_FORMAT_MOD_INVALID)) {
		format = strip_alpha_channel(format);
	}
	const struct wlr_drm_format *plane_format =
		wlr_drm_format_set_get(&plane->formats, format);
	if (plane_format == NULL) {
		wlr_log(WLR_ERROR, "Plane %"PRIu32" doesn't support format 0x%"PRIX32,
			plane->id, format);
		return false;
	}

	const struct wlr_drm_format_set *render_formats =
		wlr_renderer_get_dmabuf_render_formats(drm->renderer.wlr_rend);
	if (render_formats == NULL) {
		wlr_log(WLR_ERROR, "Failed to get render formats");
		return false;
	}
	const struct wlr_drm_format *render_format =
		wlr_drm_format_set_get(render_formats, format);
	if (render_format == NULL) {
		wlr_log(WLR_ERROR, "Renderer doesn't support format 0x%"PRIX32,
			format);
		return false;
	}

	struct wlr_drm_format *drm_format = NULL;
	if (with_modifiers) {
		drm_format = wlr_drm_format_intersect(plane_format, render_format);
		if (drm_format == NULL) {
			wlr_log(WLR_ERROR,
				"Failed to intersect plane and render formats 0x%"PRIX32,
				format);
			return false;
		}
	} else {
		const struct wlr_drm_format format_no_modifiers = { .format = format };
		drm_format = wlr_drm_format_dup(&format_no_modifiers);
	}

	drm_plane_finish_surface(plane);

	bool ok = true;
	if (!drm->parent) {
		ok = init_drm_surface(&plane->surf, &drm->renderer, width, height,
			drm_format, flags | GBM_BO_USE_SCANOUT);
	} else {
		ok = init_drm_surface(&plane->surf, &drm->parent->renderer,
			width, height, drm_format, flags | GBM_BO_USE_LINEAR);
		if (ok && !init_drm_surface(&plane->mgpu_surf, &drm->renderer,
				width, height, drm_format, flags | GBM_BO_USE_SCANOUT)) {
			finish_drm_surface(&plane->surf);
			ok = false;
		}
	}

	free(drm_format);

	return ok;
}

void drm_fb_clear(struct wlr_drm_fb *fb) {
	if (!fb->bo) {
		assert(!fb->wlr_buf);
		return;
	}

	gbm_bo_destroy(fb->bo);
	wlr_buffer_unlock(fb->wlr_buf);

	fb->wlr_buf = NULL;
	fb->bo = NULL;

	if (fb->mgpu_bo) {
		assert(fb->mgpu_surf);
		gbm_bo_destroy(fb->mgpu_bo);
		wlr_buffer_unlock(fb->mgpu_wlr_buf);
		fb->mgpu_bo = NULL;
		fb->mgpu_wlr_buf = NULL;
		fb->mgpu_surf = NULL;
	}
}

bool drm_fb_lock_surface(struct wlr_drm_fb *fb, struct wlr_drm_surface *surf) {
	assert(surf->back_buffer != NULL);

	struct wlr_buffer *buffer = wlr_buffer_lock(surf->back_buffer);

	// Unset the current EGL context ASAP, because other operations may require
	// making another context current.
	drm_surface_unset_current(surf);

	bool ok = drm_fb_import_wlr(fb, surf->renderer, buffer, NULL);
	wlr_buffer_unlock(buffer);
	return ok;
}

bool drm_fb_import_wlr(struct wlr_drm_fb *fb, struct wlr_drm_renderer *renderer,
		struct wlr_buffer *buf, struct wlr_drm_format_set *set) {
	drm_fb_clear(fb);

	struct wlr_dmabuf_attributes attribs;
	if (!wlr_buffer_get_dmabuf(buf, &attribs)) {
		return false;
	}

	if (set && !wlr_drm_format_set_has(set, attribs.format, attribs.modifier)) {
		// The format isn't supported by the plane. Try stripping the alpha
		// channel, if any.
		uint32_t format = strip_alpha_channel(attribs.format);
		if (wlr_drm_format_set_has(set, format, attribs.modifier)) {
			attribs.format = format;
		} else {
			return false;
		}
	}

	if (attribs.modifier != DRM_FORMAT_MOD_INVALID ||
			attribs.n_planes > 1 || attribs.offset[0] != 0) {
		struct gbm_import_fd_modifier_data data = {
			.width = attribs.width,
			.height = attribs.height,
			.format = attribs.format,
			.num_fds = attribs.n_planes,
			.modifier = attribs.modifier,
		};

		if ((size_t)attribs.n_planes > sizeof(data.fds) / sizeof(data.fds[0])) {
			return false;
		}

		for (size_t i = 0; i < (size_t)attribs.n_planes; ++i) {
			data.fds[i] = attribs.fd[i];
			data.strides[i] = attribs.stride[i];
			data.offsets[i] = attribs.offset[i];
		}

		fb->bo = gbm_bo_import(renderer->gbm, GBM_BO_IMPORT_FD_MODIFIER,
			&data, GBM_BO_USE_SCANOUT);
	} else {
		struct gbm_import_fd_data data = {
			.fd = attribs.fd[0],
			.width = attribs.width,
			.height = attribs.height,
			.stride = attribs.stride[0],
			.format = attribs.format,
		};

		fb->bo = gbm_bo_import(renderer->gbm, GBM_BO_IMPORT_FD,
			&data, GBM_BO_USE_SCANOUT);
	}

	if (!fb->bo) {
		return false;
	}

	fb->wlr_buf = wlr_buffer_lock(buf);

	return true;
}

void drm_fb_move(struct wlr_drm_fb *new, struct wlr_drm_fb *old) {
	drm_fb_clear(new);

	*new = *old;
	memset(old, 0, sizeof(*old));
}

bool drm_surface_render_black_frame(struct wlr_drm_surface *surf) {
	if (!drm_surface_make_current(surf, NULL)) {
		return false;
	}

	struct wlr_renderer *renderer = surf->renderer->wlr_rend;
	wlr_renderer_begin(renderer, surf->width, surf->height);
	wlr_renderer_clear(renderer, (float[]){ 0.0, 0.0, 0.0, 1.0 });
	wlr_renderer_end(renderer);

	return true;
}

struct gbm_bo *drm_fb_acquire(struct wlr_drm_fb *fb, struct wlr_drm_backend *drm,
		struct wlr_drm_surface *mgpu) {
	if (!fb->bo) {
		wlr_log(WLR_ERROR, "Tried to acquire an FB with a NULL BO");
		return NULL;
	}

	if (!drm->parent) {
		return fb->bo;
	}

	if (fb->mgpu_bo) {
		return fb->mgpu_bo;
	}

	/* Perform copy across GPUs */

	struct wlr_texture *tex = get_tex_for_bo(mgpu->renderer, fb->bo);
	if (!tex) {
		return NULL;
	}

	if (!drm_surface_make_current(mgpu, NULL)) {
		return NULL;
	}

	float mat[9];
	wlr_matrix_projection(mat, 1, 1, WL_OUTPUT_TRANSFORM_NORMAL);

	struct wlr_renderer *renderer = mgpu->renderer->wlr_rend;
	wlr_renderer_begin(renderer, mgpu->width, mgpu->height);
	wlr_renderer_clear(renderer, (float[]){ 0.0, 0.0, 0.0, 0.0 });
	wlr_render_texture_with_matrix(renderer, tex, mat, 1.0f);
	wlr_renderer_end(renderer);

	struct wlr_drm_fb mgpu_fb = {
		.bo = fb->mgpu_bo,
		.wlr_buf = fb->mgpu_wlr_buf,
	};
	if (!drm_fb_lock_surface(&mgpu_fb, mgpu)) {
		return false;
	}
	fb->mgpu_bo = mgpu_fb.bo;
	fb->mgpu_wlr_buf = mgpu_fb.wlr_buf;
	fb->mgpu_surf = mgpu;
	return fb->mgpu_bo;
}
