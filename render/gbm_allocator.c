#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <drm_fourcc.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <wlr/config.h>
#include <wlr/util/log.h>
#include "render/gbm_allocator.h"

static bool export_gbm_bo(struct gbm_bo *bo,
		struct wlr_dmabuf_attributes *out) {
	struct wlr_dmabuf_attributes attribs = {0};
	int i;

#if WLR_HAS_LATEST_GBM
	attribs.n_planes = gbm_bo_get_plane_count(bo);
	if (attribs.n_planes > WLR_DMABUF_MAX_PLANES) {
		wlr_log(WLR_ERROR, "GBM BO contains too many planes (%d)",
			attribs.n_planes);
		return false;
	}

	attribs.width = gbm_bo_get_width(bo);
	attribs.height = gbm_bo_get_height(bo);
	attribs.format = gbm_bo_get_format(bo);
	attribs.modifier = gbm_bo_get_modifier(bo);

	int32_t handle = -1;
	for (i = 0; i < attribs.n_planes; ++i) {
		// GBM is lacking a function to get a FD for a given plane. Instead,
		// check all planes have the same handle. We can't use
		// drmPrimeHandleToFD because that messes up handle ref'counting in
		// the user-space driver.
		// TODO: use gbm_bo_get_plane_fd when it lands, see
		// https://gitlab.freedesktop.org/mesa/mesa/-/merge_requests/5442
		union gbm_bo_handle plane_handle = gbm_bo_get_handle_for_plane(bo, i);
		if (plane_handle.s32 < 0) {
			wlr_log(WLR_ERROR, "gbm_bo_get_handle_for_plane failed");
			goto error_fd;
		}
		if (i == 0) {
			handle = plane_handle.s32;
		} else if (plane_handle.s32 != handle) {
			wlr_log(WLR_ERROR, "Failed to export GBM BO: "
				"all planes don't have the same GEM handle");
			goto error_fd;
		}

		attribs.fd[i] = gbm_bo_get_fd(bo);
		if (attribs.fd[i] < 0) {
			wlr_log(WLR_ERROR, "gbm_bo_get_fd failed");
			goto error_fd;
		}

		attribs.offset[i] = gbm_bo_get_offset(bo, i);
		attribs.stride[i] = gbm_bo_get_stride_for_plane(bo, i);
	}
#else
	/*
	 * I think those driver came with a legacy API won't support
	 * allocating a multiple planes buffer, ex. ARM Mali won't.
	 */
	attribs.n_planes = i = 1;
	attribs.width = gbm_bo_get_width(bo);
	attribs.height = gbm_bo_get_height(bo);
	attribs.format = gbm_bo_get_format(bo);

	attribs.fd[0] = gbm_bo_get_fd(bo);
	if (attribs.fd[0] < 0) {
		wlr_log(WLR_ERROR, "gbm_bo_get_fd failed");
		goto error_fd;
	}

#endif

	memcpy(out, &attribs, sizeof(attribs));
	return true;

error_fd:
	for (int j = 0; j < i; ++j) {
		close(attribs.fd[j]);
	}
	return false;
}

static struct gbm_bo *get_bo_for_dmabuf(struct gbm_device *gbm,
					struct wlr_dmabuf_attributes *attribs)
{
	if (attribs->modifier != DRM_FORMAT_MOD_INVALID ||
	    attribs->n_planes > 1 || attribs->offset[0] != 0)
	{
#if WLR_HAS_LATEST_GBM
		struct gbm_import_fd_modifier_data data = {
			.width = attribs->width,
			.height = attribs->height,
			.format = attribs->format,
			.num_fds = attribs->n_planes,
			.modifier = attribs->modifier,
		};

		if ((size_t)attribs->n_planes >
		    sizeof(data.fds) / sizeof(data.fds[0]))
			return false;

		for (size_t i = 0; i < (size_t)attribs->n_planes; ++i) {
			data.fds[i] = attribs->fd[i];
			data.strides[i] = attribs->stride[i];
			data.offsets[i] = attribs->offset[i];
		}

		return gbm_bo_import(gbm, GBM_BO_IMPORT_FD_MODIFIER,
				     &data, GBM_BO_USE_SCANOUT);
#else
		wlr_log(WLR_DEBUG,
			"This GBM driver doesn't support a format with modifiers");
		return NULL;
#endif
	} else {
		struct gbm_import_fd_data data = {
			.fd = attribs->fd[0],
			.width = attribs->width,
			.height = attribs->height,
			.stride = attribs->stride[0],
			.format = attribs->format,
		};

		return gbm_bo_import(gbm, GBM_BO_IMPORT_FD, &data,
				     GBM_BO_USE_SCANOUT);
	}
}

static uint32_t get_fb_for_bo_legacy(struct gbm_bo *bo) {
	struct gbm_device *gbm = NULL;
	uint32_t width, height, depth, bpp, pitch, handle;
	uint32_t fb_id = 0;
	int fd = -1;

	bpp = 32;
	/* We only support this as a fallback of last resort for ARGB8888 visuals,
	 * like xf86-video-modesetting does. This is necessary on BE machines. */
#if WLR_HAS_LATEST_GBM
	if (gbm_bo_get_plane_count(bo) != 1) {
		wlr_log(WLR_DEBUG,
			"Invalid layout %x (%d planes) requested for legacy DRM framebuffer",
			gbm_bo_get_format(bo), gbm_bo_get_plane_count(bo));
		return 0;
	}
	bpp = gbm_bo_get_bpp(bo);
#endif
	width = gbm_bo_get_width(bo);
	height = gbm_bo_get_height(bo);
	depth = 32;
	pitch = gbm_bo_get_stride(bo);
	handle = gbm_bo_get_handle(bo).u32;

	gbm = gbm_bo_get_device(bo);
	fd = gbm_device_get_fd(gbm);

	if (drmModeAddFB(fd, width, height, depth, bpp, pitch, handle, &fb_id)) {
		wlr_log_errno(WLR_ERROR, "Unable to add DRM framebuffer");
	}

	return fb_id;
}

static uint32_t get_fb_for_bo(struct gbm_bo *bo)
{
	struct gbm_device *gbm = gbm_bo_get_device(bo);

	int fd = gbm_device_get_fd(gbm);
	uint32_t width = gbm_bo_get_width(bo);
	uint32_t height = gbm_bo_get_height(bo);
	uint32_t format = gbm_bo_get_format(bo);

	uint32_t handles[4] = {0};
	uint32_t strides[4] = {0};
	uint32_t offsets[4] = {0};
	uint64_t modifiers[4] = {0};
	bool flag = false;

	uint32_t fb_id = 0;

#if WLR_HAS_LATEST_GBM
	for (int i = 0; i < gbm_bo_get_plane_count(bo); i++) {
		handles[i] = gbm_bo_get_handle_for_plane(bo, i).u32;
		strides[i] = gbm_bo_get_stride_for_plane(bo, i);
		offsets[i] = gbm_bo_get_offset(bo, i);
		// KMS requires all BO planes to have the same modifier
		modifiers[i] = gbm_bo_get_modifier(bo);
	}
	if (modifiers[0] != DRM_FORMAT_MOD_INVALID && modifiers[0]) {
		flag = true;
	}
#else
	handles[0] = gbm_bo_get_handle(bo).u32;
	strides[0] = gbm_bo_get_stride(bo);
#endif

	if (drmModeAddFB2WithModifiers(fd, width, height, format, handles,
				       strides, offsets,
				       flag ? modifiers : NULL,
				       &fb_id,
				       flag ? DRM_MODE_FB_MODIFIERS : 0))
	{
		if (!flag && (format == GBM_FORMAT_ARGB8888)) {
			wlr_log_errno(WLR_DEBUG,
				"Unable to add DRM framebuffer, trying legacy method");
			fb_id = get_fb_for_bo_legacy(bo);
		}
	}

	if (!fb_id) {
		wlr_log_errno(WLR_ERROR,
			"Unable to add DRM framebuffer: %s (%d)",
			strerror(errno), errno);
	}

	return fb_id;
}

static void wlr_gbm_destroy(void *data)
{
	struct gbm_bo *bo = (struct gbm_bo *)data;
	gbm_bo_destroy(bo);
}

static struct wlr_drm_buffer *create_buffer(struct wlr_gbm_allocator *alloc,
		int width, int height, const struct wlr_drm_format *format)
{
	struct gbm_bo *bo = NULL;
	struct wlr_drm_buffer *buf = NULL;
	bool has_modifier = true;

	struct gbm_device *gbm_device = alloc->gbm_device;
#if WLR_HAS_LATEST_GBM
	if (format->len > 0) {
		bo = gbm_bo_create_with_modifiers(gbm_device, width, height,
			format->format, format->modifiers, format->len);
	}
#endif
	if (bo == NULL) {
		uint32_t usage = GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING;
		if (format->len == 1 &&
                    format->modifiers[0] == DRM_FORMAT_MOD_LINEAR)
			usage |= GBM_BO_USE_LINEAR;

		bo = gbm_bo_create(gbm_device, width, height, format->format,
				   usage);
		has_modifier = false;
	}
	if (bo == NULL) {
		wlr_log(WLR_ERROR, "gbm_bo_create failed");
		return NULL;
	}

	buf = wlr_drm_buffer_create(bo, wlr_gbm_destroy);
	if (!buf) {
		gbm_bo_destroy(bo);
		return NULL;
	}

	if (!export_gbm_bo(bo, &buf->dmabuf)) {
		wlr_buffer_drop(&buf->base);
		return NULL;
	}

	buf->fb_id = get_fb_for_bo(bo);
	if (!buf->fb_id) {
		wlr_buffer_drop(&buf->base);
		return NULL;
	}
	buf->drm_fd = alloc->fd;

	// If the buffer has been allocated with an implicit modifier, make sure we
	// don't populate the modifier field: other parts of the stack may not
	// understand modifiers, and they can't strip the modifier.
	if (!has_modifier) {
		buf->dmabuf.modifier = DRM_FORMAT_MOD_INVALID;
	}

	buf->base.width = width;
	buf->base.height = height;
	wl_list_insert(&alloc->buffers, &buf->link);

	wlr_log(WLR_DEBUG, "Allocated %dx%d GBM buffer (format 0x%"PRIX32", "
		"modifier 0x%"PRIX64")", buf->base.width, buf->base.height,
		buf->dmabuf.format, buf->dmabuf.modifier);

	return buf;
}

static const struct wlr_allocator_interface allocator_impl;

static struct wlr_gbm_allocator *get_gbm_alloc_from_alloc(
		struct wlr_allocator *alloc) {
	assert(alloc->impl == &allocator_impl);
	return (struct wlr_gbm_allocator *)alloc;
}

struct wlr_gbm_allocator *wlr_gbm_allocator_create(int fd) {
	uint64_t cap;
	if (drmGetCap(fd, DRM_CAP_PRIME, &cap) ||
			!(cap & DRM_PRIME_CAP_EXPORT)) {
		wlr_log(WLR_ERROR, "PRIME export not supported");
		return NULL;
	}

	struct wlr_gbm_allocator *alloc = calloc(1, sizeof(*alloc));
	if (alloc == NULL) {
		return NULL;
	}
	wlr_allocator_init(&alloc->base, &allocator_impl);

	alloc->fd = fd;
	wl_list_init(&alloc->buffers);

	alloc->gbm_device = gbm_create_device(fd);
	if (alloc->gbm_device == NULL) {
		wlr_log(WLR_ERROR, "gbm_create_device failed");
		free(alloc);
		return NULL;
	}

	wlr_log(WLR_DEBUG, "Created GBM allocator with backend %s",
		gbm_device_get_backend_name(alloc->gbm_device));

	return alloc;
}

static void allocator_destroy(struct wlr_allocator *wlr_alloc) {
	struct wlr_gbm_allocator *alloc = get_gbm_alloc_from_alloc(wlr_alloc);

	// The gbm_bo objects need to be destroyed before the gbm_device
	struct wlr_drm_buffer *buf, *buf_tmp;
	wl_list_for_each_safe(buf, buf_tmp, &alloc->buffers, link) {
		wlr_buffer_drop(&buf->base);
	}

	gbm_device_destroy(alloc->gbm_device);
	close(alloc->fd);
	free(alloc);
}

static struct wlr_buffer *allocator_create_buffer(
		struct wlr_allocator *wlr_alloc, int width, int height,
		const struct wlr_drm_format *format) {
	struct wlr_gbm_allocator *alloc = get_gbm_alloc_from_alloc(wlr_alloc);
	struct wlr_drm_buffer *buffer = create_buffer(alloc, width, height, format);

	if (!buffer)
		return NULL;

	return &buffer->base;
}

static struct wlr_drm_buffer *
allocator_import_buffer(struct wlr_allocator *wlr_alloc,
		        struct wlr_buffer *wlr_buf) {
	struct wlr_gbm_allocator *alloc = get_gbm_alloc_from_alloc(wlr_alloc);
	struct wlr_drm_buffer *buf = NULL;
	struct wlr_dmabuf_attributes attribs = { 0, };
	struct gbm_bo *bo = NULL;
	uint32_t fb_id = 0;

	buf = wlr_drm_buffer_cast(wlr_buf);
	if (buf) {
		return buf;
	}

	if (!wlr_buffer_get_dmabuf(wlr_buf, &attribs)) {
		wlr_log(WLR_DEBUG, "%p is not a DMAbuf", wlr_buf);
		return NULL;
	}

	bo = get_bo_for_dmabuf(alloc->gbm_device, &attribs);
	if (!bo) {
		return NULL;
	}

	fb_id = get_fb_for_bo(bo);
	if (!fb_id) {
		goto error;
	}

	buf = wlr_drm_buffer_create(bo, wlr_gbm_destroy);
	if (!buf) {
		goto error;
	}

	wlr_buffer_lock(wlr_buf);
	buf->orig_buf = wlr_buf;
	buf->base.width = wlr_buf->width;
	buf->base.height = wlr_buf->height;
	buf->drm_fd = alloc->fd;
	buf->fb_id = fb_id;

	wl_list_insert(&alloc->buffers, &buf->link);

	wlr_log(WLR_DEBUG,
		"%p is imported as a %dx%d GBM buffer (format 0x%"PRIX32", ""modifier 0x%"PRIX64")",
		wlr_buf, buf->base.width, buf->base.height,
		buf->dmabuf.format, buf->dmabuf.modifier);

	return buf;
error:
	wlr_log(WLR_DEBUG, "%p can't be imported", wlr_buf);
	gbm_bo_destroy(bo);
	return NULL;
}

static const struct wlr_allocator_interface allocator_impl = {
	.destroy = allocator_destroy,
	.create_buffer = allocator_create_buffer,
	.import_buffer = allocator_import_buffer,
};
