#ifndef BACKEND_DRM_RENDERER_H
#define BACKEND_DRM_RENDERER_H

#include <stdbool.h>
#include <stdint.h>
#include <render/allocator.h>
#include <wlr/backend.h>
#include <render/allocator.h>
#include <wlr/render/wlr_renderer.h>

struct wlr_drm_backend;
struct wlr_drm_plane;
struct wlr_buffer;

struct wlr_drm_renderer {
	struct wlr_drm_backend *backend;

	struct wlr_renderer *wlr_rend;
	struct wlr_allocator *alloc;
};

struct wlr_drm_surface {
	struct wlr_drm_renderer *renderer;

	uint32_t width;
	uint32_t height;

	struct wlr_swapchain *swapchain;
	struct wlr_buffer *back_buffer;
};

bool init_drm_renderer(struct wlr_drm_backend *drm,
	struct wlr_drm_renderer *renderer);
void finish_drm_renderer(struct wlr_drm_renderer *renderer);

bool drm_surface_make_current(struct wlr_drm_surface *surf, int *buffer_age);
void drm_surface_unset_current(struct wlr_drm_surface *surf);

/**
 * We don't track whether this buffer has been imported unless the source
 * buffer is a wlr_drm_buffer impl buffer.
 */
bool drm_fb_import(struct wlr_drm_buffer **fb, struct wlr_drm_backend *drm,
                   struct wlr_buffer *buf,
		   const struct wlr_drm_format_set *formats);

void drm_fb_destroy(struct wlr_drm_buffer *fb);

void drm_fb_clear(struct wlr_drm_buffer **fb);
void drm_fb_move(struct wlr_drm_buffer **new, struct wlr_drm_buffer **old);

bool drm_surface_render_black_frame(struct wlr_drm_surface *surf);

bool drm_plane_init_surface(struct wlr_drm_plane *plane,
		struct wlr_drm_backend *drm, int32_t width, uint32_t height,
		uint32_t format, bool with_modifiers);
void drm_plane_finish_surface(struct wlr_drm_plane *plane);
bool drm_plane_lock_surface(struct wlr_drm_plane *plane,
		struct wlr_drm_backend *drm);

#endif
