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
	wlr_buffer_init(&buffer->base, &buffer_impl, width, height);
	wl_list_insert(&alloc->buffers, &buffer->link);

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

	buffer->width = create.width;
	buffer->height = create.height;

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
		.width = buffer->width,
		.height = buffer->height,
		.format = format->format,
		.n_planes = 1,
		.offset[0] = 0,
		.stride[0] = buffer->stride,
		.fd[0] = prime_fd,
	};

	wlr_log(WLR_DEBUG, "Allocated %u x %u DRM dumb buffer (GEM %" PRIu32 ")",
			buffer->width, buffer->height, buffer->handle);

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
		buf->drm_fd = -1;
		wl_list_remove(&buf->link);
		wl_list_init(&buf->link);
	}

	close(alloc->drm_fd);
	free(alloc);
}

static const struct wlr_allocator_interface allocator_impl = {
	.create_buffer = allocator_create_buffer,
	.destroy = allocator_destroy,
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
