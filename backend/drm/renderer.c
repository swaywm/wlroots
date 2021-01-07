#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <drm_fourcc.h>
#include <fcntl.h>
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
#include "backend/drm/util.h"
#include "render/drm_format_set.h"
#include "render/gbm_allocator.h"
#include "render/swapchain.h"
#include "render/wlr_renderer.h"

bool init_drm_renderer(struct wlr_drm_backend *drm,
		struct wlr_drm_renderer *renderer) {
	renderer->backend = drm;

	renderer->gbm = gbm_create_device(drm->fd);
	if (!renderer->gbm) {
		wlr_log(WLR_ERROR, "Failed to create GBM device");
		return false;
	}

	renderer->wlr_rend = wlr_renderer_autocreate(EGL_PLATFORM_GBM_KHR,
			renderer->gbm);
	if (!renderer->wlr_rend) {
		wlr_log(WLR_ERROR, "Failed to create EGL/WLR renderer");
		goto error_gbm;
	}

	int alloc_fd = fcntl(drm->fd, F_DUPFD_CLOEXEC, 0);
	if (alloc_fd < 0) {
		wlr_log_errno(WLR_ERROR, "fcntl(F_DUPFD_CLOEXEC) failed");
		goto error_wlr_rend;
	}

	renderer->allocator = wlr_gbm_allocator_create(alloc_fd);
	if (renderer->allocator == NULL) {
		wlr_log(WLR_ERROR, "Failed to create allocator");
		close(alloc_fd);
		goto error_wlr_rend;
	}

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
	gbm_device_destroy(renderer->gbm);
}

static bool init_drm_surface(struct wlr_drm_surface *surf,
		struct wlr_drm_renderer *renderer, uint32_t width, uint32_t height,
		const struct wlr_drm_format *drm_format) {
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

	surf->swapchain = wlr_swapchain_create(&renderer->allocator->base,
		width, height, drm_format);
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

	struct wlr_egl *egl = wlr_gles2_renderer_get_egl(surf->renderer->wlr_rend);
	if (!wlr_egl_make_current(egl, EGL_NO_SURFACE, NULL)) {
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
	struct wlr_egl *egl = wlr_gles2_renderer_get_egl(surf->renderer->wlr_rend);

	wlr_renderer_bind_buffer(surf->renderer->wlr_rend, NULL);
	wlr_egl_unset_current(egl);

	wlr_buffer_unlock(surf->back_buffer);
	surf->back_buffer = NULL;
}

static struct wlr_buffer *drm_surface_blit(struct wlr_drm_surface *surf,
		struct wlr_buffer *buffer) {
	struct wlr_renderer *renderer = surf->renderer->wlr_rend;

	if (surf->width != (uint32_t)buffer->width ||
			surf->height != (uint32_t)buffer->height) {
		wlr_log(WLR_ERROR, "Surface size doesn't match buffer size");
		return NULL;
	}

	struct wlr_dmabuf_attributes attribs = {0};
	if (!wlr_buffer_get_dmabuf(buffer, &attribs)) {
		return NULL;
	}

	struct wlr_texture *tex = wlr_texture_from_dmabuf(renderer, &attribs);
	if (tex == NULL) {
		return NULL;
	}

	if (!drm_surface_make_current(surf, NULL)) {
		wlr_texture_destroy(tex);
		return NULL;
	}

	float mat[9];
	wlr_matrix_projection(mat, 1, 1, WL_OUTPUT_TRANSFORM_NORMAL);

	wlr_renderer_begin(renderer, surf->width, surf->height);
	wlr_renderer_clear(renderer, (float[]){ 0.0, 0.0, 0.0, 0.0 });
	wlr_render_texture_with_matrix(renderer, tex, mat, 1.0f);
	wlr_renderer_end(renderer);

	assert(surf->back_buffer != NULL);
	struct wlr_buffer *out = wlr_buffer_lock(surf->back_buffer);

	drm_surface_unset_current(surf);

	wlr_texture_destroy(tex);

	return out;
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

static struct wlr_drm_format *create_linear_format(uint32_t format) {
	struct wlr_drm_format *fmt = wlr_drm_format_create(format);
	if (fmt == NULL) {
		return NULL;
	}
	if (!wlr_drm_format_add(&fmt, DRM_FORMAT_MOD_LINEAR)) {
		free(fmt);
		return NULL;
	}
	return fmt;
}

bool drm_plane_init_surface(struct wlr_drm_plane *plane,
		struct wlr_drm_backend *drm, int32_t width, uint32_t height,
		uint32_t format, bool with_modifiers) {
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

	struct wlr_drm_format *format_implicit_modifier = NULL;
	if (!with_modifiers) {
		format_implicit_modifier = wlr_drm_format_create(format);
		render_format = format_implicit_modifier;
	}

	struct wlr_drm_format *drm_format =
		wlr_drm_format_intersect(plane_format, render_format);
	if (drm_format == NULL) {
		wlr_log(WLR_ERROR,
			"Failed to intersect plane and render formats 0x%"PRIX32,
			format);
		free(format_implicit_modifier);
		return false;
	}

	drm_plane_finish_surface(plane);

	bool ok = true;
	if (!drm->parent) {
		ok = init_drm_surface(&plane->surf, &drm->renderer,
			width, height, drm_format);
	} else {
		struct wlr_drm_format *drm_format_linear = create_linear_format(format);
		if (drm_format_linear == NULL) {
			free(drm_format);
			free(format_implicit_modifier);
			return false;
		}

		ok = init_drm_surface(&plane->surf, &drm->parent->renderer,
			width, height, drm_format_linear);
		free(drm_format_linear);

		if (ok && !init_drm_surface(&plane->mgpu_surf, &drm->renderer,
				width, height, drm_format)) {
			finish_drm_surface(&plane->surf);
			ok = false;
		}
	}

	free(drm_format);
	free(format_implicit_modifier);

	return ok;
}

void drm_fb_clear(struct wlr_drm_fb *fb) {
	if (!fb->bo) {
		assert(!fb->wlr_buf);
		return;
	}

	struct gbm_device *gbm = gbm_bo_get_device(fb->bo);
	if (drmModeRmFB(gbm_device_get_fd(gbm), fb->id) != 0) {
		wlr_log(WLR_ERROR, "drmModeRmFB failed");
	}

	gbm_bo_destroy(fb->bo);
	wlr_buffer_unlock(fb->wlr_buf);
	wlr_buffer_unlock(fb->mgpu_wlr_buf);

	memset(fb, 0, sizeof(*fb));
}

bool drm_fb_lock_surface(struct wlr_drm_fb *fb, struct wlr_drm_backend *drm,
		struct wlr_drm_surface *surf, struct wlr_drm_surface *mgpu) {
	assert(surf->back_buffer != NULL);

	struct wlr_buffer *buffer = wlr_buffer_lock(surf->back_buffer);

	// Unset the current EGL context ASAP, because other operations may require
	// making another context current.
	drm_surface_unset_current(surf);

	bool ok = drm_fb_import(fb, drm, buffer, mgpu, NULL);
	wlr_buffer_unlock(buffer);
	return ok;
}

static struct gbm_bo *get_bo_for_dmabuf(struct gbm_device *gbm,
		struct wlr_dmabuf_attributes *attribs) {
	if (attribs->modifier != DRM_FORMAT_MOD_INVALID ||
			attribs->n_planes > 1 || attribs->offset[0] != 0) {
		struct gbm_import_fd_modifier_data data = {
			.width = attribs->width,
			.height = attribs->height,
			.format = attribs->format,
			.num_fds = attribs->n_planes,
			.modifier = attribs->modifier,
		};

		if ((size_t)attribs->n_planes > sizeof(data.fds) / sizeof(data.fds[0])) {
			return false;
		}

		for (size_t i = 0; i < (size_t)attribs->n_planes; ++i) {
			data.fds[i] = attribs->fd[i];
			data.strides[i] = attribs->stride[i];
			data.offsets[i] = attribs->offset[i];
		}

		return gbm_bo_import(gbm, GBM_BO_IMPORT_FD_MODIFIER,
			&data, GBM_BO_USE_SCANOUT);
	} else {
		struct gbm_import_fd_data data = {
			.fd = attribs->fd[0],
			.width = attribs->width,
			.height = attribs->height,
			.stride = attribs->stride[0],
			.format = attribs->format,
		};

		return gbm_bo_import(gbm, GBM_BO_IMPORT_FD, &data, GBM_BO_USE_SCANOUT);
	}
}

bool drm_fb_import(struct wlr_drm_fb *fb, struct wlr_drm_backend *drm,
		struct wlr_buffer *buf, struct wlr_drm_surface *mgpu,
		struct wlr_drm_format_set *set) {
	drm_fb_clear(fb);

	fb->wlr_buf = wlr_buffer_lock(buf);

	if (drm->parent && mgpu) {
		// Perform a copy across GPUs
		fb->mgpu_wlr_buf = drm_surface_blit(mgpu, buf);
		if (!fb->mgpu_wlr_buf) {
			wlr_log(WLR_ERROR, "Failed to blit buffer across GPUs");
			goto error_mgpu_wlr_buf;
		}

		buf = fb->mgpu_wlr_buf;
	}

	struct wlr_dmabuf_attributes attribs;
	if (!wlr_buffer_get_dmabuf(buf, &attribs)) {
		wlr_log(WLR_ERROR, "Failed to get DMA-BUF from buffer");
		goto error_get_dmabuf;
	}

	if (set && !wlr_drm_format_set_has(set, attribs.format, attribs.modifier)) {
		// The format isn't supported by the plane. Try stripping the alpha
		// channel, if any.
		uint32_t format = strip_alpha_channel(attribs.format);
		if (wlr_drm_format_set_has(set, format, attribs.modifier)) {
			attribs.format = format;
		} else {
			wlr_log(WLR_ERROR, "Buffer format 0x%"PRIX32" cannot be scanned out",
				attribs.format);
			goto error_get_dmabuf;
		}
	}

	fb->bo = get_bo_for_dmabuf(drm->renderer.gbm, &attribs);
	if (!fb->bo) {
		wlr_log(WLR_ERROR, "Failed to import DMA-BUF in GBM");
		goto error_get_dmabuf;
	}

	fb->id = get_fb_for_bo(fb->bo, drm->addfb2_modifiers);
	if (!fb->id) {
		wlr_log(WLR_ERROR, "Failed to import GBM BO in KMS");
		goto error_get_fb_for_bo;
	}

	return true;

error_get_fb_for_bo:
	gbm_bo_destroy(fb->bo);
error_get_dmabuf:
	wlr_buffer_unlock(fb->mgpu_wlr_buf);
error_mgpu_wlr_buf:
	wlr_buffer_unlock(fb->wlr_buf);
	memset(fb, 0, sizeof(*fb));
	return false;
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
