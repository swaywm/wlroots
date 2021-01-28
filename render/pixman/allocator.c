#include "render/pixman.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static const struct wlr_buffer_impl buffer_impl;

static struct wlr_pixman_buffer *pixman_buffer_from_buffer(
		struct wlr_buffer *wlr_buf) {
	assert(wlr_buf->impl == &buffer_impl);
	return (struct wlr_pixman_buffer *)wlr_buf;
}

static bool buffer_get_dmabuf(struct wlr_buffer *wlr_buf,
		struct wlr_dmabuf_attributes *out) {
	struct wlr_pixman_buffer *buf = pixman_buffer_from_buffer(wlr_buf);
	memcpy(out, &buf->dmabuf, sizeof(buf->dmabuf));
	return true;
}

static void buffer_destroy(struct wlr_buffer *wlr_buf) {
	struct wlr_pixman_buffer *buf = pixman_buffer_from_buffer(wlr_buf);
	wlr_dmabuf_attributes_finish(&buf->dmabuf);
	free(buf);
}

static const struct wlr_buffer_impl buffer_impl = {
	.destroy = buffer_destroy,
	.get_dmabuf = buffer_get_dmabuf,
};

static const struct wlr_allocator_interface allocator_impl;

static struct wlr_pixman_allocator *pixman_alloc_from_alloc(
		struct wlr_allocator *wlr_alloc) {
	assert(wlr_alloc->impl == &allocator_impl);
	return (struct wlr_pixman_allocator *)wlr_alloc;
}

static struct wlr_buffer *allocator_create_buffer(
		struct wlr_allocator *wlr_alloc, int width, int height,
		const struct wlr_drm_format *drm_format) {
	//struct wlr_pixman_allocator *alloc = pixman_alloc_from_alloc(wlr_alloc);

	return NULL;
}

static void allocator_destroy(struct wlr_allocator *wlr_alloc) {
	struct wlr_pixman_allocator *alloc = pixman_alloc_from_alloc(wlr_alloc);
	free(alloc);
}

static const struct wlr_allocator_interface allocator_impl = {
	.create_buffer = allocator_create_buffer,
	.destroy = allocator_destroy,
};

struct wlr_pixman_allocator *wlr_pixman_allocator_create(int drm_fd) {
	struct wlr_pixman_allocator *alloc = calloc(1, sizeof(*alloc));
	if (alloc == NULL) {
		return NULL;
	}
	wlr_allocator_init(&alloc->base, &allocator_impl);

	return alloc;
}
