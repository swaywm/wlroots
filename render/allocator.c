#include <assert.h>
#include <stdlib.h>
#include <wlr/util/log.h>
#include <xf86drm.h>
#include "backend/backend.h"
#include "render/allocator.h"
#include "render/gbm_allocator.h"
#include "render/shm_allocator.h"
#include "render/drm_dumb_allocator.h"
#include "render/wlr_renderer.h"
#include "types/wlr_buffer.h"

void wlr_allocator_init(struct wlr_allocator *alloc,
		const struct wlr_allocator_interface *impl, uint32_t buffer_caps) {
	assert(impl && impl->destroy && impl->create_buffer);
	alloc->impl = impl;
	alloc->buffer_caps = buffer_caps;
	wl_signal_init(&alloc->events.destroy);
}

struct wlr_allocator *allocator_autocreate_with_drm_fd(
		struct wlr_backend *backend, struct wlr_renderer *renderer,
		int drm_fd) {
	uint32_t backend_caps = backend_get_buffer_caps(backend);
	uint32_t renderer_caps = renderer_get_render_buffer_caps(renderer);

	struct wlr_allocator *alloc = NULL;
	uint32_t gbm_caps = WLR_BUFFER_CAP_DMABUF;
	if ((backend_caps & gbm_caps) && (renderer_caps & gbm_caps)
			&& drm_fd != -1) {
		wlr_log(WLR_DEBUG, "Trying to create gbm allocator");
		if ((alloc = wlr_gbm_allocator_create(drm_fd)) != NULL) {
			return alloc;
		}
		wlr_log(WLR_DEBUG, "Failed to create gbm allocator");
	}

	uint32_t shm_caps = WLR_BUFFER_CAP_SHM | WLR_BUFFER_CAP_DATA_PTR;
	if ((backend_caps & shm_caps) && (renderer_caps & shm_caps)) {
		wlr_log(WLR_DEBUG, "Trying to create shm allocator");
		if ((alloc = wlr_shm_allocator_create()) != NULL) {
			return alloc;
		}
		wlr_log(WLR_DEBUG, "Failed to create shm allocator");
	}

	uint32_t drm_caps = WLR_BUFFER_CAP_DMABUF | WLR_BUFFER_CAP_DATA_PTR;
	if ((backend_caps & drm_caps) && (renderer_caps & drm_caps)
			&& drm_fd != -1) {
		wlr_log(WLR_DEBUG, "Trying to create drm dumb allocator");
		if ((alloc = wlr_drm_dumb_allocator_create(drm_fd)) != NULL) {
			return alloc;
		}
		wlr_log(WLR_DEBUG, "Failed to create drm dumb allocator");
	}

	wlr_log(WLR_ERROR, "Failed to create allocator");
	return NULL;
}

struct wlr_allocator *wlr_allocator_autocreate(struct wlr_backend *backend,
		struct wlr_renderer *renderer) {
	// Note, drm_fd may be negative if unavailable
	int drm_fd = wlr_backend_get_drm_fd(backend);
	return allocator_autocreate_with_drm_fd(backend, renderer, drm_fd);
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
	struct wlr_buffer *buffer =
		alloc->impl->create_buffer(alloc, width, height, format);
	if (buffer == NULL) {
		return NULL;
	}
	if (alloc->buffer_caps & WLR_BUFFER_CAP_DATA_PTR) {
		assert(buffer->impl->get_data_ptr);
	}
	if (alloc->buffer_caps & WLR_BUFFER_CAP_DMABUF) {
		assert(buffer->impl->get_dmabuf);
	}
	if (alloc->buffer_caps & WLR_BUFFER_CAP_SHM) {
		assert(buffer->impl->get_shm);
	}
	return buffer;
}
