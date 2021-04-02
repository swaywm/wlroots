#include <assert.h>
#include <stdlib.h>
#include "wlr/config.h"
#include <string.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include "render/allocator.h"
#include "render/gbm_allocator.h"
#include "render/drm_dumb_allocator.h"

static const struct wlr_buffer_impl drm_buffer_impl;

void wlr_allocator_init(struct wlr_allocator *alloc,
		const struct wlr_allocator_interface *impl) {
	assert(impl && impl->destroy && impl->create_buffer);
	alloc->impl = impl;
	wl_signal_init(&alloc->events.destroy);
}

void wlr_allocator_destroy(struct wlr_allocator *alloc) {
	if (alloc == NULL) {
		return;
	}
	wl_signal_emit(&alloc->events.destroy, NULL);
	alloc->impl->destroy(alloc);
}

struct wlr_buffer *wlr_allocator_create_buffer(struct wlr_allocator *alloc,
		int width, int height, const struct wlr_drm_format *format) {
	return alloc->impl->create_buffer(alloc, width, height, format);
}

struct wlr_allocator *wlr_allocator_create_with_drm_fd(int fd)
{
	struct wlr_allocator *alloc = NULL;

#if WLR_HAS_GBM_SUPPORT
	struct wlr_gbm_allocator *gbm_alloc = NULL;
	gbm_alloc = wlr_gbm_allocator_create(fd);
	if (gbm_alloc)
		alloc = &gbm_alloc->base;
#else
	struct wlr_drm_dumb_allocator *dumb_alloc = NULL;
	dumb_alloc = wlr_drm_dumb_allocator_create(fd);
	if (dumb_alloc)
		alloc = &dumb_alloc->base;
#endif

	return alloc;
}

struct wlr_drm_buffer *wlr_allocator_import(struct wlr_allocator *alloc,
					    struct wlr_buffer *buf)
{
	return alloc->impl->import_buffer(alloc, buf);
}

static struct wlr_drm_buffer *get_drm_buffer(struct wlr_buffer *wlr_buffer)
{
	struct wlr_drm_buffer *buffer = NULL;

	return wl_container_of(wlr_buffer, buffer, base);
}

static bool drm_buffer_get_dmabuf(struct wlr_buffer *wlr_buffer,
				  struct wlr_dmabuf_attributes *attribs)
{
	struct wlr_drm_buffer *buf = get_drm_buffer(wlr_buffer);
	if (!buf)
		return false;

	if (buf->dmabuf.n_planes)
		return memcpy(attribs, &buf->dmabuf, sizeof(buf->dmabuf));

	if (buf->orig_buf)
		return wlr_buffer_get_dmabuf(buf->orig_buf, attribs);

	return false;
}

static void drm_buffer_destroy(struct wlr_buffer *wlr_buf)
{
	struct wlr_drm_buffer *buf = get_drm_buffer(wlr_buf);

	wlr_dmabuf_attributes_finish(&buf->dmabuf);

	if (buf->fb_id &&(buf->drm_fd >= 0))
		drmModeRmFB(buf->drm_fd, buf->fb_id);

	if (buf->impl_data && buf->destroy_impl_data) {
		(*buf->destroy_impl_data)(buf->impl_data);
		buf->impl_data = NULL;
	}

	if (buf->orig_buf) {
		wlr_buffer_unlock(buf->orig_buf);
		buf->orig_buf = NULL;
	}

	wl_list_remove(&buf->link);
	free(buf);
}

static const struct wlr_buffer_impl drm_buffer_impl = {
	.destroy = drm_buffer_destroy,
	.get_dmabuf = drm_buffer_get_dmabuf,
};

struct wlr_drm_buffer *wlr_drm_buffer_cast(struct wlr_buffer *wlr_buffer)
{
	if (wlr_buffer->impl == (&drm_buffer_impl))
		return get_drm_buffer(wlr_buffer);
	return NULL;
}

struct wlr_drm_buffer *wlr_drm_buffer_create(void *data,
					     void (*destroy_impl_data)(void *))
{
	struct wlr_drm_buffer *buf = NULL;

	buf = calloc(1, sizeof(buf));
	if (!buf)
		goto bail;

	buf->impl_data = data;
	buf->destroy_impl_data = destroy_impl_data;

	wlr_buffer_init(&buf->base, &drm_buffer_impl, -1, -1);

bail:
	return buf;
}
