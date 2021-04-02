#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <drm_fourcc.h>
#include <fcntl.h>
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
#include "render/pixel_format.h"
#include "render/swapchain.h"
#include "render/wlr_renderer.h"

bool init_drm_renderer(struct wlr_drm_backend *drm,
		struct wlr_drm_renderer *renderer)
{
	renderer->backend = drm;

	renderer->wlr_rend = wlr_renderer_autocreate_with_drm_fd(drm->fd);
	if (!renderer->wlr_rend) {
		wlr_log(WLR_ERROR, "Failed to create EGL/WLR renderer");
		goto error;
	}

	int alloc_fd = fcntl(drm->fd, F_DUPFD_CLOEXEC, 0);
	if (alloc_fd < 0) {
		wlr_log_errno(WLR_ERROR, "fcntl(F_DUPFD_CLOEXEC) failed");
		goto error_wlr_rend;
	}

	renderer->alloc = wlr_allocator_create_with_drm_fd(alloc_fd);
	if (renderer->alloc == NULL) {
		wlr_log(WLR_ERROR, "Failed to create allocator");
		close(alloc_fd);
		goto error_wlr_rend;
	}

	return true;

error_wlr_rend:
	wlr_renderer_destroy(renderer->wlr_rend);
error:
	return false;
}

void finish_drm_renderer(struct wlr_drm_renderer *renderer) {
	if (!renderer) {
		return;
	}

	wlr_allocator_destroy(renderer->alloc);
	wlr_renderer_destroy(renderer->wlr_rend);
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

	surf->swapchain = wlr_swapchain_create(renderer->alloc,
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

	if (!wlr_renderer_bind_buffer(surf->renderer->wlr_rend, surf->back_buffer)) {
		wlr_log(WLR_ERROR, "Failed to bind buffer to renderer");
		return false;
	}

	return true;
}

void drm_surface_unset_current(struct wlr_drm_surface *surf) {
	assert(surf->back_buffer != NULL);

	wlr_renderer_bind_buffer(surf->renderer->wlr_rend, NULL);

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

bool drm_plane_init_surface(struct wlr_drm_plane *plane,
		struct wlr_drm_backend *drm, int32_t width, uint32_t height,
		uint32_t format, bool with_modifiers) {
	if (!wlr_drm_format_set_has(&plane->formats, format, DRM_FORMAT_MOD_INVALID)) {
		const struct wlr_pixel_format_info *info =
			drm_get_pixel_format_info(format);
		if (!info) {
			wlr_log(WLR_ERROR,
				"Failed to fallback on DRM opaque substitute for format "
				"0x%"PRIX32, format);
			return false;
		}
		format = info->opaque_substitute;
	}

	const struct wlr_drm_format *plane_format =
		wlr_drm_format_set_get(&plane->formats, format);
	if (plane_format == NULL) {
		wlr_log(WLR_ERROR, "Plane %"PRIu32" doesn't support format 0x%"PRIX32,
			plane->id, format);
		return false;
	}

	const struct wlr_drm_format_set *render_formats =
		wlr_renderer_get_render_formats(drm->renderer.wlr_rend);
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

void drm_fb_destroy(struct wlr_drm_buffer *buffer) {
	/* It may lead this buffer be dropped */
	wlr_buffer_unlock(&buffer->base);
}

void drm_fb_clear(struct wlr_drm_buffer **buf_ptr) {
	if (*buf_ptr == NULL)
		return;

	drm_fb_destroy(*buf_ptr);

	*buf_ptr = NULL;
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

bool drm_fb_import(struct wlr_drm_buffer **buf_ptr, struct wlr_drm_backend *drm,
		   struct wlr_buffer *wlr_buf,
		   const struct wlr_drm_format_set *formats)
{
	struct wlr_drm_renderer *render = NULL;
	struct wlr_drm_buffer *buf = NULL;

	render = &drm->renderer;

	buf = wlr_allocator_import(render->alloc, wlr_buf);
	if (!buf)
		return false;

	wlr_buffer_lock(&buf->base);
	drm_fb_move(buf_ptr, &buf);
	return true;
}

void drm_fb_move(struct wlr_drm_buffer **new, struct wlr_drm_buffer **old) {
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
