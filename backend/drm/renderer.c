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
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_matrix.h>
#include <wlr/util/log.h>
#include "backend/drm/drm.h"
#include "backend/drm/util.h"
#include "render/drm_format_set.h"
#include "render/allocator.h"
#include "render/pixel_format.h"
#include "render/swapchain.h"
#include "render/wlr_renderer.h"
#include "render/wlr_texture.h"

bool init_drm_renderer(struct wlr_drm_backend *drm,
		struct wlr_drm_renderer *renderer) {
	renderer->backend = drm;

	renderer->gbm = gbm_create_device(drm->fd);
	if (!renderer->gbm) {
		wlr_log(WLR_ERROR, "Failed to create GBM device");
		return false;
	}

	renderer->wlr_rend = renderer_autocreate_with_drm_fd(drm->fd);
	if (!renderer->wlr_rend) {
		wlr_log(WLR_ERROR, "Failed to create renderer");
		goto error_gbm;
	}

	renderer->allocator = allocator_autocreate_with_drm_fd(&drm->backend,
		renderer->wlr_rend, drm->fd);
	if (renderer->allocator == NULL) {
		wlr_log(WLR_ERROR, "Failed to create allocator");
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

	wlr_allocator_destroy(renderer->allocator);
	wlr_renderer_destroy(renderer->wlr_rend);
	gbm_device_destroy(renderer->gbm);
}

bool init_drm_surface(struct wlr_drm_surface *surf,
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

	surf->swapchain = wlr_swapchain_create(renderer->allocator, width, height,
			drm_format);
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

	if (!renderer_bind_buffer(surf->renderer->wlr_rend, surf->back_buffer)) {
		wlr_log(WLR_ERROR, "Failed to bind buffer to renderer");
		return false;
	}

	return true;
}

void drm_surface_unset_current(struct wlr_drm_surface *surf) {
	assert(surf->back_buffer != NULL);

	renderer_bind_buffer(surf->renderer->wlr_rend, NULL);

	wlr_buffer_unlock(surf->back_buffer);
	surf->back_buffer = NULL;
}

struct wlr_buffer *drm_surface_blit(struct wlr_drm_surface *surf,
		struct wlr_buffer *buffer) {
	struct wlr_renderer *renderer = surf->renderer->wlr_rend;

	if (surf->width != (uint32_t)buffer->width ||
			surf->height != (uint32_t)buffer->height) {
		wlr_log(WLR_ERROR, "Surface size doesn't match buffer size");
		return NULL;
	}

	struct wlr_texture *tex = wlr_texture_from_buffer(renderer, buffer);
	if (tex == NULL) {
		return NULL;
	}

	if (!drm_surface_make_current(surf, NULL)) {
		wlr_texture_destroy(tex);
		return NULL;
	}

	float mat[9];
	wlr_matrix_identity(mat);
	wlr_matrix_scale(mat, surf->width, surf->height);

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

struct wlr_drm_format *drm_plane_pick_render_format(
		struct wlr_drm_plane *plane, struct wlr_drm_renderer *renderer) {
	const struct wlr_drm_format_set *render_formats =
		wlr_renderer_get_render_formats(renderer->wlr_rend);
	if (render_formats == NULL) {
		wlr_log(WLR_ERROR, "Failed to get render formats");
		return NULL;
	}

	const struct wlr_drm_format_set *plane_formats = &plane->formats;

	uint32_t fmt = DRM_FORMAT_ARGB8888;
	if (!wlr_drm_format_set_has(&plane->formats, fmt, DRM_FORMAT_MOD_INVALID)) {
		const struct wlr_pixel_format_info *format_info =
			drm_get_pixel_format_info(fmt);
		assert(format_info != NULL &&
			format_info->opaque_substitute != DRM_FORMAT_INVALID);
		fmt = format_info->opaque_substitute;
	}

	const struct wlr_drm_format *render_format =
		wlr_drm_format_set_get(render_formats, fmt);
	if (render_format == NULL) {
		wlr_log(WLR_DEBUG, "Renderer doesn't support format 0x%"PRIX32, fmt);
		return NULL;
	}

	const struct wlr_drm_format *plane_format =
		wlr_drm_format_set_get(plane_formats, fmt);
	if (plane_format == NULL) {
		wlr_log(WLR_DEBUG, "Plane %"PRIu32" doesn't support format 0x%"PRIX32,
			plane->id, fmt);
		return NULL;
	}

	struct wlr_drm_format *format =
		wlr_drm_format_intersect(plane_format, render_format);
	if (format == NULL) {
		wlr_log(WLR_DEBUG, "Failed to intersect plane and render "
			"modifiers for format 0x%"PRIX32, fmt);
		return NULL;
	}

	return format;
}

bool drm_plane_init_surface(struct wlr_drm_plane *plane,
		struct wlr_drm_backend *drm, int32_t width, uint32_t height,
		bool with_modifiers) {
	struct wlr_drm_format *format =
		drm_plane_pick_render_format(plane, &drm->renderer);
	if (format == NULL) {
		wlr_log(WLR_ERROR, "Failed to pick render format for plane %"PRIu32,
			plane->id);
		return false;
	}

	if (!with_modifiers) {
		struct wlr_drm_format *format_implicit_modifier =
			wlr_drm_format_create(format->format);
		free(format);
		format = format_implicit_modifier;
	}

	drm_plane_finish_surface(plane);

	bool ok = true;
	if (!drm->parent) {
		ok = init_drm_surface(&plane->surf, &drm->renderer,
			width, height, format);
	} else {
		struct wlr_drm_format *format_linear = create_linear_format(format->format);
		if (format_linear == NULL) {
			free(format);
			return false;
		}

		ok = init_drm_surface(&plane->surf, &drm->parent->renderer,
			width, height, format_linear);
		free(format_linear);

		if (ok && !init_drm_surface(&plane->mgpu_surf, &drm->renderer,
				width, height, format)) {
			finish_drm_surface(&plane->surf);
			ok = false;
		}
	}

	free(format);

	return ok;
}

void drm_fb_clear(struct wlr_drm_fb **fb_ptr) {
	if (*fb_ptr == NULL) {
		return;
	}

	struct wlr_drm_fb *fb = *fb_ptr;
	wlr_buffer_unlock(fb->wlr_buf); // may destroy the buffer

	*fb_ptr = NULL;
}

bool drm_plane_lock_surface(struct wlr_drm_plane *plane,
		struct wlr_drm_backend *drm) {
	assert(plane->surf.back_buffer != NULL);
	struct wlr_buffer *buf = wlr_buffer_lock(plane->surf.back_buffer);

	// Unset the current EGL context ASAP, because other operations may require
	// making another context current.
	drm_surface_unset_current(&plane->surf);

	struct wlr_buffer *local_buf;
	if (drm->parent) {
		// Perform a copy across GPUs
		local_buf = drm_surface_blit(&plane->mgpu_surf, buf);
		if (!local_buf) {
			wlr_log(WLR_ERROR, "Failed to blit buffer across GPUs");
			return false;
		}
	} else {
		local_buf = wlr_buffer_lock(buf);
	}
	wlr_buffer_unlock(buf);

	bool ok = drm_fb_import(&plane->pending_fb, drm, local_buf, NULL);
	if (!ok) {
		wlr_log(WLR_ERROR, "Failed to import buffer");
	}
	wlr_buffer_unlock(local_buf);
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

static void drm_fb_handle_wlr_buf_destroy(struct wl_listener *listener,
		void *data) {
	struct wlr_drm_fb *fb = wl_container_of(listener, fb, wlr_buf_destroy);
	drm_fb_destroy(fb);
}

static struct wlr_drm_fb *drm_fb_create(struct wlr_drm_backend *drm,
		struct wlr_buffer *buf, const struct wlr_drm_format_set *formats) {
	struct wlr_drm_fb *fb = calloc(1, sizeof(*fb));
	if (!fb) {
		wlr_log_errno(WLR_ERROR, "Allocation failed");
		return NULL;
	}

	struct wlr_dmabuf_attributes attribs;
	if (!wlr_buffer_get_dmabuf(buf, &attribs)) {
		wlr_log(WLR_DEBUG, "Failed to get DMA-BUF from buffer");
		goto error_get_dmabuf;
	}

	if (attribs.flags != 0) {
		wlr_log(WLR_DEBUG, "Buffer with DMA-BUF flags 0x%"PRIX32" cannot be "
			"scanned out", attribs.flags);
		goto error_get_dmabuf;
	}

	if (formats && !wlr_drm_format_set_has(formats, attribs.format,
			attribs.modifier)) {
		// The format isn't supported by the plane. Try stripping the alpha
		// channel, if any.
		const struct wlr_pixel_format_info *info =
			drm_get_pixel_format_info(attribs.format);
		if (info != NULL && info->opaque_substitute != DRM_FORMAT_INVALID &&
				wlr_drm_format_set_has(formats, info->opaque_substitute, attribs.modifier)) {
			attribs.format = info->opaque_substitute;
		} else {
			wlr_log(WLR_DEBUG, "Buffer format 0x%"PRIX32" with modifier "
				"0x%"PRIX64" cannot be scanned out",
				attribs.format, attribs.modifier);
			goto error_get_dmabuf;
		}
	}

	fb->bo = get_bo_for_dmabuf(drm->renderer.gbm, &attribs);
	if (!fb->bo) {
		wlr_log(WLR_DEBUG, "Failed to import DMA-BUF in GBM");
		goto error_get_dmabuf;
	}

	fb->id = get_fb_for_bo(fb->bo, drm->addfb2_modifiers);
	if (!fb->id) {
		wlr_log(WLR_DEBUG, "Failed to import GBM BO in KMS");
		goto error_get_fb_for_bo;
	}

	fb->wlr_buf = buf;

	fb->wlr_buf_destroy.notify = drm_fb_handle_wlr_buf_destroy;
	wl_signal_add(&buf->events.destroy, &fb->wlr_buf_destroy);

	wl_list_insert(&drm->fbs, &fb->link);

	return fb;

error_get_fb_for_bo:
	gbm_bo_destroy(fb->bo);
error_get_dmabuf:
	free(fb);
	return NULL;
}

void drm_fb_destroy(struct wlr_drm_fb *fb) {
	wl_list_remove(&fb->link);
	wl_list_remove(&fb->wlr_buf_destroy.link);

	struct gbm_device *gbm = gbm_bo_get_device(fb->bo);
	if (drmModeRmFB(gbm_device_get_fd(gbm), fb->id) != 0) {
		wlr_log(WLR_ERROR, "drmModeRmFB failed");
	}

	gbm_bo_destroy(fb->bo);
	free(fb);
}

static struct wlr_drm_fb *drm_fb_get(struct wlr_drm_backend *drm,
		struct wlr_buffer *local_buf) {
	struct wlr_drm_fb *fb;
	wl_list_for_each(fb, &drm->fbs, link) {
		if (fb->wlr_buf == local_buf) {
			return fb;
		}
	}
	return NULL;
}

bool drm_fb_import(struct wlr_drm_fb **fb_ptr, struct wlr_drm_backend *drm,
		struct wlr_buffer *buf, const struct wlr_drm_format_set *formats) {
	struct wlr_drm_fb *fb = drm_fb_get(drm, buf);
	if (!fb) {
		fb = drm_fb_create(drm, buf, formats);
		if (!fb) {
			return false;
		}
	}

	wlr_buffer_lock(buf);
	drm_fb_move(fb_ptr, &fb);
	return true;
}

void drm_fb_move(struct wlr_drm_fb **new, struct wlr_drm_fb **old) {
	drm_fb_clear(new);
	*new = *old;
	*old = NULL;
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
