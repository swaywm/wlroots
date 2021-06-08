#ifndef BACKEND_DRM_RENDERER_H
#define BACKEND_DRM_RENDERER_H

#include <gbm.h>
#include <stdbool.h>
#include <stdint.h>
#include <wlr/backend.h>
#include <wlr/render/wlr_renderer.h>

struct wlr_drm_backend;
struct wlr_drm_plane;
struct wlr_buffer;

struct wlr_drm_renderer {
	struct wlr_drm_backend *backend;
	struct gbm_device *gbm;

	struct wlr_renderer *wlr_rend;
	struct wlr_allocator *allocator;
};

struct wlr_drm_surface {
	struct wlr_drm_renderer *renderer;

	uint32_t width;
	uint32_t height;

	struct wlr_swapchain *swapchain;
	struct wlr_buffer *back_buffer;
};

struct wlr_drm_fb {
	struct wlr_buffer *wlr_buf;
	struct wl_list link; // wlr_drm_backend.fbs

	struct gbm_bo *bo;
	uint32_t id;

	struct wl_listener wlr_buf_destroy;
};

bool init_drm_renderer(struct wlr_drm_backend *drm,
	struct wlr_drm_renderer *renderer);
void finish_drm_renderer(struct wlr_drm_renderer *renderer);

bool init_drm_surface(struct wlr_drm_surface *surf,
	struct wlr_drm_renderer *renderer, uint32_t width, uint32_t height,
	const struct wlr_drm_format *drm_format);
bool drm_surface_make_current(struct wlr_drm_surface *surf, int *buffer_age);
void drm_surface_unset_current(struct wlr_drm_surface *surf);

bool drm_fb_import(struct wlr_drm_fb **fb, struct wlr_drm_backend *drm,
		struct wlr_buffer *buf, const struct wlr_drm_format_set *formats);
void drm_fb_destroy(struct wlr_drm_fb *fb);

void drm_fb_clear(struct wlr_drm_fb **fb);
void drm_fb_move(struct wlr_drm_fb **new, struct wlr_drm_fb **old);

struct wlr_buffer *drm_surface_blit(struct wlr_drm_surface *surf,
	struct wlr_buffer *buffer);

struct wlr_drm_format *drm_plane_pick_render_format(
		struct wlr_drm_plane *plane, struct wlr_drm_renderer *renderer);
void drm_plane_finish_surface(struct wlr_drm_plane *plane);

#endif
