
#ifndef RENDER_EGLSTREAM_ALLOCATOR_H
#define RENDER_EGLSTREAM_ALLOCATOR_H

#include <wlr/types/wlr_buffer.h>
#include "render/allocator.h"
#include "wlr/render/egl.h"
#include "render/gbm_allocator.h"


struct wlr_eglstreams_allocator;

struct wlr_eglstream_plane {
	struct wlr_eglstream stream;
	uint32_t id;
	struct wl_list link; // wlr_eglstreams_allocator.planes
	uint32_t locks;
	struct wlr_eglstreams_allocator *alloc;
	int width;
	int height;
};

struct wlr_eglstream_buffer {
	struct wlr_buffer base;
	struct wlr_eglstream_plane *plane;
};

struct wlr_eglstreams_allocator {
	// Dumb gdb allocator just not to change the rest of api
	struct wlr_gbm_allocator base_gbm;

	struct wlr_drm_backend *drm;
	struct wl_list planes;
};

/**
 * Creates a new EGLStreams allocator from a DRM Renderer.
 * TODO: All creators should return generic wlr_allocator.
 */
struct wlr_gbm_allocator *
	wlr_eglstreams_allocator_create(struct wlr_drm_backend *drm);

/**
 * Returns configured plane for given id if any.
 */
struct wlr_eglstream_plane *wlr_eglstream_plane_for_id(
		struct wlr_allocator *wlr_alloc, uint32_t plane_id);

#endif

