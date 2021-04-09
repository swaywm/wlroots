#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <drm_fourcc.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wlr/util/log.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include "render/drm_dumb_allocator.h"
#include "render/pixel_format.h"

static const struct wlr_buffer_impl buffer_impl;

static struct wlr_drm_dumb_buffer *drm_dumb_buffer_from_buffer(
		struct wlr_buffer *wlr_buf) {
	assert(wlr_buf->impl == &buffer_impl);
	return (struct wlr_drm_dumb_buffer *)wlr_buf;
}

static void finish_buffer(struct wlr_drm_dumb_buffer *buf) {
	if (buf->data) {
		munmap(buf->data, buf->size);
	}

	if (buf->drm_fd >= 0) {
		struct drm_mode_destroy_dumb destroy = { .handle = buf->handle };
		if (drmIoctl(buf->drm_fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy)) {
			wlr_log_errno(WLR_ERROR, "failed to destroy drm dumb buffer");
		}
	}

	wl_list_remove(&buf->link);
}

static struct wlr_drm_dumb_buffer *create_buffer(
		struct wlr_drm_dumb_allocator *alloc, int width, int height,
		const struct wlr_drm_format *format) {
	struct wlr_drm_dumb_buffer *buffer = calloc(1, sizeof(*buffer));
	if (buffer == NULL) {
		return NULL;
	}

	const struct wlr_pixel_format_info *info = drm_get_pixel_format_info(
			format->format);
	if (info == NULL) {
		wlr_log(WLR_ERROR, "drm format not supported");
		goto create_err;
	}

	struct drm_mode_create_dumb create = {0};
	create.width = (uint32_t)width;
	create.height = (uint32_t)height;
	create.bpp = info->bpp;

	if (drmIoctl(alloc->drm_fd, DRM_IOCTL_MODE_CREATE_DUMB, &create) != 0) {
		wlr_log_errno(WLR_ERROR, "failed to create drm dumb buffer");
		goto create_err;
	}

	wlr_buffer_init(&buffer->base, &buffer_impl, width, height);
	wl_list_insert(&alloc->buffers, &buffer->link);

	buffer->stride = create.pitch;
	buffer->handle = create.handle;

	buffer->drm_fd = alloc->drm_fd;

	struct drm_mode_map_dumb map = {0};
	map.handle = buffer->handle;

	if (drmIoctl(alloc->drm_fd, DRM_IOCTL_MODE_MAP_DUMB, &map) != 0) {
		wlr_log_errno(WLR_ERROR, "failed to map drm dumb buffer");
		goto create_destroy;
	}

	buffer->data = mmap(NULL, create.size, PROT_READ | PROT_WRITE, MAP_SHARED,
			alloc->drm_fd, map.offset);
	if (buffer->data == MAP_FAILED) {
		wlr_log_errno(WLR_ERROR, "failed to mmap drm dumb buffer");
		goto create_destroy;
	}

	buffer->size = create.size;

	memset(buffer->data, 0, create.size);

	int prime_fd;
	if (drmPrimeHandleToFD(alloc->drm_fd, buffer->handle, DRM_CLOEXEC,
			&prime_fd) != 0) {
		wlr_log_errno(WLR_ERROR, "failed to get prime handle from GEM");
		goto create_destroy;
	}

	buffer->dmabuf = (struct wlr_dmabuf_attributes){
		.width = create.width,
		.height = create.height,
		.format = format->format,
		.n_planes = 1,
		.offset[0] = 0,
		.stride[0] = buffer->stride,
		.fd[0] = prime_fd,
	};

	wlr_log(WLR_DEBUG, "Allocated %u x %u DRM dumb buffer (GEM %" PRIu32 ")",
			width, height, buffer->handle);

	return buffer;
create_destroy:
	finish_buffer(buffer);
create_err:
	free(buffer);
	return NULL;
}

static bool buffer_get_data_ptr(struct wlr_buffer *wlr_buffer, void **data,
		size_t *stride) {
	struct wlr_drm_dumb_buffer *buf = drm_dumb_buffer_from_buffer(wlr_buffer);
	*data = buf->data;
	*stride = buf->stride;
	return true;
}

static bool buffer_get_dmabuf(struct wlr_buffer *wlr_buffer,
		struct wlr_dmabuf_attributes *attribs) {
	struct wlr_drm_dumb_buffer *buf = drm_dumb_buffer_from_buffer(wlr_buffer);
	memcpy(attribs, &buf->dmabuf, sizeof(buf->dmabuf));
	return true;
}

static void buffer_destroy(struct wlr_buffer *wlr_buffer) {
	struct wlr_drm_dumb_buffer *buf = drm_dumb_buffer_from_buffer(wlr_buffer);
	finish_buffer(buf);
	free(buf);
}

static const struct wlr_buffer_impl buffer_impl = {
	.destroy = buffer_destroy,
	.get_dmabuf = buffer_get_dmabuf,
	.get_data_ptr = buffer_get_data_ptr,
};

static const struct wlr_allocator_interface allocator_impl;

static struct wlr_drm_dumb_allocator *drm_dumb_alloc_from_alloc(
		struct wlr_allocator *wlr_alloc) {
	assert(wlr_alloc->impl == &allocator_impl);
	return (struct wlr_drm_dumb_allocator *)wlr_alloc;
}

static struct wlr_buffer *allocator_create_buffer(
		struct wlr_allocator *wlr_alloc, int width, int height,
		const struct wlr_drm_format *drm_format) {
	struct wlr_drm_dumb_allocator *alloc = drm_dumb_alloc_from_alloc(wlr_alloc);
	struct wlr_drm_dumb_buffer *buffer = create_buffer(alloc, width, height,
			drm_format);
	if (buffer == NULL) {
		return NULL;
	}
	return &buffer->base;
}

static void allocator_destroy(struct wlr_allocator *wlr_alloc) {
	struct wlr_drm_dumb_allocator *alloc = drm_dumb_alloc_from_alloc(wlr_alloc);

	struct wlr_drm_dumb_buffer *buf, *buf_tmp;
	wl_list_for_each_safe(buf, buf_tmp, &alloc->buffers, link) {
		wlr_buffer_drop(&buf->base);
	}

	close(alloc->drm_fd);
	free(alloc);
}

static struct wlr_drm_buffer *
allocator_import_buffer(struct wlr_allocator *wlr_alloc,
		struct wlr_buffer *wlr_buf) {
	struct wlr_drm_dumb_allocator *alloc = NULL;
	struct wlr_drm_buffer *buf = NULL;
	struct wlr_drm_dumb_buffer *dumb_buf = NULL;
	struct wlr_dmabuf_attributes attribs = { 0, };

	uint32_t w, h, fmt;
	uint32_t handles[4] = {0};
	uint32_t strides[4] = {0};
	uint32_t offsets[4] = {0};
	uint64_t modifiers[4] = {0};
	uint32_t fb_id = 0;
	uint8_t i = 0;
	bool flag = false;
	int ret = 0;

	buf = wlr_drm_buffer_cast(wlr_buf);
	if (buf)
		return buf;

	alloc = drm_dumb_alloc_from_alloc(wlr_alloc);

	dumb_buf = drm_dumb_buffer_from_buffer(wlr_buf);
	if (dumb_buf) {
		handles[0] = dumb_buf->handle;
		w = dumb_buf->dmabuf.width;
		h = dumb_buf->dmabuf.height;
		fmt = dumb_buf->dmabuf.format;
		goto do_import;
	}

	if (!wlr_buffer_get_dmabuf(wlr_buf, &attribs)) {
		wlr_log(WLR_DEBUG, "%p is not a DMAbuf", wlr_buf);
		return NULL;
	}

	for (i = 0; i < attribs.n_planes && i < 4; i++) {
		ret = drmPrimeFDToHandle (alloc->drm_fd,
				          attribs.fd[i], &handles[i]);
		if (ret) {
			goto import_fd_failed;
		}
		strides[i] = attribs.stride[i];
		offsets[i] = attribs.offset[i];
		modifiers[i] = attribs.modifier;
	}
	w = attribs.width;
	h = attribs.height;
	fmt = attribs.format;

	if (modifiers[0] != DRM_FORMAT_MOD_INVALID && modifiers[0]) {
		flag = true;
	}
do_import:
	buf = wlr_drm_buffer_create(NULL, NULL);
	wlr_buffer_lock(wlr_buf);
	buf->orig_buf = wlr_buf;
	buf->base.width = wlr_buf->width;
	buf->base.height = wlr_buf->height;
	buf->drm_fd = alloc->drm_fd;

	ret = drmModeAddFB2WithModifiers(alloc->drm_fd, w, h, fmt, handles,
			strides, offsets, flag ? modifiers : NULL, &fb_id,
			flag ? DRM_MODE_FB_MODIFIERS : 0);

	if (ret || !fb_id) {
		goto error;
	}

	buf->fb_id = fb_id;
	/* Close those gem_handle */
	if (!dumb_buf) {
		for (i = 0; i < attribs.n_planes && i < 4; i++) {
			struct drm_gem_close arg = { handles[i], };

			ret = drmIoctl(alloc->drm_fd, DRM_IOCTL_GEM_CLOSE,
				&arg);

			if (ret) {
				wlr_log(WLR_INFO,
					"Failed to close GEM handle: %s %d",
					strerror(errno), errno);
			}

			handles[i] = 0;
		}
	}

	/* TODO: cache it in allocator's list */
	wlr_log(WLR_DEBUG,
		"%p is imported as a %dx%d GBM buffer (format 0x%"PRIX32", ""modifier 0x%"PRIX64")",
		wlr_buf, buf->base.width, buf->base.height,
		buf->dmabuf.format, buf->dmabuf.modifier);

	return buf;

import_fd_failed:
	wlr_log(WLR_ERROR,
		"Failed to import prime fd %d: %s (%d)",
		handles[i], strerror(errno), errno);

error:
	if (!dumb_buf) {
		for (i = 0; i < attribs.n_planes && i < 4; i++) {
			struct drm_gem_close arg = { handles[i], };

			ret = drmIoctl(alloc->drm_fd, DRM_IOCTL_GEM_CLOSE,
				&arg);

			if (ret) {
				wlr_log(WLR_INFO,
					"Failed to close GEM handle: %s %d",
					strerror(errno), errno);
			}

			handles[i] = 0;
		}
	}
	wlr_buffer_drop(&buf->base);
	return NULL;
}

static const struct wlr_allocator_interface allocator_impl = {
	.create_buffer = allocator_create_buffer,
	.destroy = allocator_destroy,
	.import_buffer = allocator_import_buffer,
};

struct wlr_drm_dumb_allocator *wlr_drm_dumb_allocator_create(int drm_fd) {
	uint64_t has_dumb = 0;
	if (drmGetCap(drm_fd, DRM_CAP_DUMB_BUFFER, &has_dumb) < 0) {
		wlr_log(WLR_ERROR, "Failed to get drm capabilities");
		return NULL;
	}

	if (has_dumb == 0) {
		wlr_log(WLR_ERROR, "drm dumb buffers not supported");
		return NULL;
	}

	struct wlr_drm_dumb_allocator *alloc = calloc(1, sizeof(*alloc));
	if (alloc == NULL) {
		return NULL;
	}
	wlr_allocator_init(&alloc->base, &allocator_impl);

	alloc->drm_fd = drm_fd;
	wl_list_init(&alloc->buffers);

	return alloc;
}
