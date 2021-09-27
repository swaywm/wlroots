#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <drm_fourcc.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wayland-util.h>
#include <wlr/render/pixel_format.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_matrix.h>
#include <wlr/util/log.h>
#include "backend/drm/drm.h"
#include "backend/drm/util.h"
#include "render/drm_format_set.h"
#include "render/allocator/allocator.h"
#include "render/swapchain.h"
#include "render/wlr_renderer.h"

bool init_drm_renderer(struct wlr_drm_backend *drm,
		struct wlr_drm_renderer *renderer) {
	renderer->backend = drm;

	renderer->wlr_rend = renderer_autocreate_with_drm_fd(drm->fd);
	if (!renderer->wlr_rend) {
		wlr_log(WLR_ERROR, "Failed to create renderer");
		return false;
	}

	renderer->allocator = allocator_autocreate_with_drm_fd(&drm->backend,
		renderer->wlr_rend, drm->fd);
	if (renderer->allocator == NULL) {
		wlr_log(WLR_ERROR, "Failed to create allocator");
		wlr_renderer_destroy(renderer->wlr_rend);
		return false;
	}

	return true;
}

void finish_drm_renderer(struct wlr_drm_renderer *renderer) {
	if (!renderer) {
		return;
	}

	wlr_allocator_destroy(renderer->allocator);
	wlr_renderer_destroy(renderer->wlr_rend);
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

	wlr_swapchain_destroy(surf->swapchain);

	memset(surf, 0, sizeof(*surf));
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

	struct wlr_buffer *dst = wlr_swapchain_acquire(surf->swapchain, NULL);
	if (!dst) {
		wlr_texture_destroy(tex);
		return NULL;
	}

	float mat[9];
	wlr_matrix_identity(mat);
	wlr_matrix_scale(mat, surf->width, surf->height);

	if (!wlr_renderer_begin_with_buffer(renderer, dst)) {
		wlr_buffer_unlock(dst);
		wlr_texture_destroy(tex);
		return NULL;
	}

	wlr_renderer_clear(renderer, (float[]){ 0.0, 0.0, 0.0, 0.0 });
	wlr_render_texture_with_matrix(renderer, tex, mat, 1.0f);

	wlr_renderer_end(renderer);

	wlr_texture_destroy(tex);

	return dst;
}


void drm_plane_finish_surface(struct wlr_drm_plane *plane) {
	if (!plane) {
		return;
	}

	drm_fb_clear(&plane->pending_fb);
	drm_fb_clear(&plane->queued_fb);
	drm_fb_clear(&plane->current_fb);

	finish_drm_surface(&plane->mgpu_surf);
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
			wlr_pixel_format_info_from_drm(fmt);
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

void drm_fb_clear(struct wlr_drm_fb **fb_ptr) {
	if (*fb_ptr == NULL) {
		return;
	}

	struct wlr_drm_fb *fb = *fb_ptr;
	wlr_buffer_unlock(fb->wlr_buf); // may destroy the buffer

	*fb_ptr = NULL;
}

static void drm_fb_handle_destroy(struct wlr_addon *addon) {
	struct wlr_drm_fb *fb = wl_container_of(addon, fb, addon);
	drm_fb_destroy(fb);
}

static const struct wlr_addon_interface fb_addon_impl = {
	.name = "wlr_drm_fb",
	.destroy = drm_fb_handle_destroy,
};

static uint32_t get_bo_handle_for_fd(struct wlr_drm_backend *drm,
		int dmabuf_fd) {
	uint32_t handle = 0;
	int ret = drmPrimeFDToHandle(drm->fd, dmabuf_fd, &handle);
	if (ret != 0) {
		wlr_log_errno(WLR_DEBUG, "drmPrimeFDToHandle failed");
		return 0;
	}

	if (!drm_bo_handle_table_ref(&drm->bo_handles, handle)) {
		// If that failed, the handle wasn't ref'ed in the table previously,
		// so safe to delete
		struct drm_gem_close args = { .handle = handle };
		drmIoctl(drm->fd, DRM_IOCTL_GEM_CLOSE, &args);
		return 0;
	}

	return handle;
}

static void close_bo_handle(struct wlr_drm_backend *drm, uint32_t handle) {
	if (handle == 0) {
		return;
	}

	size_t nrefs = drm_bo_handle_table_unref(&drm->bo_handles, handle);
	if (nrefs > 0) {
		return;
	}

	struct drm_gem_close args = { .handle = handle };
	if (drmIoctl(drm->fd, DRM_IOCTL_GEM_CLOSE, &args) != 0) {
		wlr_log_errno(WLR_ERROR, "drmIoctl(GEM_CLOSE) failed");
	}
}

static uint32_t get_fb_for_bo(struct wlr_drm_backend *drm,
		struct wlr_dmabuf_attributes *dmabuf, uint32_t handles[static 4]) {
	uint64_t modifiers[4] = {0};
	for (int i = 0; i < dmabuf->n_planes; i++) {
		// KMS requires all BO planes to have the same modifier
		modifiers[i] = dmabuf->modifier;
	}

	uint32_t id = 0;
	if (drm->addfb2_modifiers && dmabuf->modifier != DRM_FORMAT_MOD_INVALID) {
		if (drmModeAddFB2WithModifiers(drm->fd, dmabuf->width, dmabuf->height,
				dmabuf->format, handles, dmabuf->stride, dmabuf->offset,
				modifiers, &id, DRM_MODE_FB_MODIFIERS) != 0) {
			wlr_log_errno(WLR_DEBUG, "drmModeAddFB2WithModifiers failed");
		}
	} else {
		int ret = drmModeAddFB2(drm->fd, dmabuf->width, dmabuf->height,
			dmabuf->format, handles, dmabuf->stride, dmabuf->offset, &id, 0);
		if (ret != 0 && dmabuf->format == DRM_FORMAT_ARGB8888 &&
				dmabuf->n_planes == 1 && dmabuf->offset[0] == 0) {
			// Some big-endian machines don't support drmModeAddFB2. Try a
			// last-resort fallback for ARGB8888 buffers, like Xorg's
			// modesetting driver does.
			wlr_log(WLR_DEBUG, "drmModeAddFB2 failed (%s), falling back to "
				"legacy drmModeAddFB", strerror(-ret));

			uint32_t depth = 32;
			uint32_t bpp = 32;
			ret = drmModeAddFB(drm->fd, dmabuf->width, dmabuf->height, depth,
				bpp, dmabuf->stride[0], handles[0], &id);
			if (ret != 0) {
				wlr_log_errno(WLR_DEBUG, "drmModeAddFB failed");
			}
		} else if (ret != 0) {
			wlr_log_errno(WLR_DEBUG, "drmModeAddFB2 failed");
		}
	}

	return id;
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
			wlr_pixel_format_info_from_drm(attribs.format);
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

	for (int i = 0; i < attribs.n_planes; ++i) {
		fb->handles[i] = get_bo_handle_for_fd(drm, attribs.fd[i]);
		if (fb->handles[i] == 0) {
			goto error_bo_handle;
		}
	}

	fb->id = get_fb_for_bo(drm, &attribs, fb->handles);
	if (!fb->id) {
		wlr_log(WLR_DEBUG, "Failed to import BO in KMS");
		goto error_bo_handle;
	}

	fb->backend = drm;
	fb->wlr_buf = buf;

	wlr_addon_init(&fb->addon, &buf->addons, drm, &fb_addon_impl);
	wl_list_insert(&drm->fbs, &fb->link);

	return fb;

error_bo_handle:
	for (int i = 0; i < attribs.n_planes; ++i) {
		close_bo_handle(drm, fb->handles[i]);
	}
error_get_dmabuf:
	free(fb);
	return NULL;
}

void drm_fb_destroy(struct wlr_drm_fb *fb) {
	struct wlr_drm_backend *drm = fb->backend;

	wl_list_remove(&fb->link);
	wlr_addon_finish(&fb->addon);

	if (drmModeRmFB(drm->fd, fb->id) != 0) {
		wlr_log(WLR_ERROR, "drmModeRmFB failed");
	}

	for (size_t i = 0; i < sizeof(fb->handles) / sizeof(fb->handles[0]); ++i) {
		close_bo_handle(drm, fb->handles[i]);
	}

	free(fb);
}

bool drm_fb_import(struct wlr_drm_fb **fb_ptr, struct wlr_drm_backend *drm,
		struct wlr_buffer *buf, const struct wlr_drm_format_set *formats) {
	struct wlr_drm_fb *fb;
	struct wlr_addon *addon = wlr_addon_find(&buf->addons, drm, &fb_addon_impl);
	if (addon != NULL) {
		fb = wl_container_of(addon, fb, addon);
	} else {
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
