#include <assert.h>
#include <drm_fourcc.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wlr/util/log.h>
#include "render/pixel_format.h"
#include "render/shm_allocator.h"
#include "util/shm.h"

static const struct wlr_buffer_impl buffer_impl;

static struct wlr_shm_buffer *shm_buffer_from_buffer(
		struct wlr_buffer *wlr_buffer) {
	assert(wlr_buffer->impl == &buffer_impl);
	return (struct wlr_shm_buffer *)wlr_buffer;
}

static void buffer_destroy(struct wlr_buffer *wlr_buffer) {
	struct wlr_shm_buffer *buffer = shm_buffer_from_buffer(wlr_buffer);
	munmap(buffer->data, buffer->size);
	close(buffer->shm.fd);
	free(buffer);
}

static bool buffer_get_shm(struct wlr_buffer *wlr_buffer,
		struct wlr_shm_attributes *shm) {
	struct wlr_shm_buffer *buffer = shm_buffer_from_buffer(wlr_buffer);
	memcpy(shm, &buffer->shm, sizeof(*shm));
	return true;
}

static bool buffer_get_data_ptr(struct wlr_buffer *wlr_buffer, void **data,
		size_t *stride) {
	struct wlr_shm_buffer *buffer = shm_buffer_from_buffer(wlr_buffer);
	*data = buffer->data;
	*stride = buffer->shm.stride;
	return true;
}

static const struct wlr_buffer_impl buffer_impl = {
	.destroy = buffer_destroy,
	.get_shm = buffer_get_shm,
	.get_data_ptr = buffer_get_data_ptr,
};

static struct wlr_buffer *allocator_create_buffer(
		struct wlr_allocator *wlr_allocator, int width, int height,
		const struct wlr_drm_format *format) {
	const struct wlr_pixel_format_info *info =
		drm_get_pixel_format_info(format->format);
	if (info == NULL) {
		wlr_log(WLR_ERROR, "Unsupported pixel format 0x%"PRIX32, format->format);
		return NULL;
	}

	struct wlr_shm_buffer *buffer = calloc(1, sizeof(*buffer));
	if (buffer == NULL) {
		return NULL;
	}
	wlr_buffer_init(&buffer->base, &buffer_impl, width, height);

	// TODO: consider using a single file for multiple buffers
	int bytes_per_pixel = info->bpp / 8;
	int stride = width * bytes_per_pixel; // TODO: align?
	buffer->size = stride * height;
	buffer->shm.fd = allocate_shm_file(buffer->size);
	if (buffer->shm.fd < 0) {
		free(buffer);
		return NULL;
	}

	buffer->shm.format = format->format;
	buffer->shm.width = width;
	buffer->shm.height = height;
	buffer->shm.stride = stride;
	buffer->shm.offset = 0;

	buffer->data = mmap(NULL, buffer->size, PROT_READ | PROT_WRITE, MAP_SHARED,
		buffer->shm.fd, 0);
	if (buffer->data == MAP_FAILED) {
		wlr_log_errno(WLR_ERROR, "mmap failed");
		close(buffer->shm.fd);
		free(buffer);
		return NULL;
	}

	return &buffer->base;
}

static void allocator_destroy(struct wlr_allocator *wlr_allocator) {
	free(wlr_allocator);
}

static const struct wlr_allocator_interface allocator_impl = {
	.destroy = allocator_destroy,
	.create_buffer = allocator_create_buffer,
};

struct wlr_shm_allocator *wlr_shm_allocator_create(void) {
	struct wlr_shm_allocator *allocator = calloc(1, sizeof(*allocator));
	if (allocator == NULL) {
		return NULL;
	}
	wlr_allocator_init(&allocator->base, &allocator_impl);

	return allocator;
}
