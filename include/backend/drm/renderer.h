#ifndef BACKEND_DRM_RENDERER_H
#define BACKEND_DRM_RENDERER_H

#include <stdbool.h>
#include <stdint.h>

#include <EGL/egl.h>
#include <gbm.h>

#include <wlr/render.h>

struct wlr_drm_backend;
struct wlr_drm_plane;

struct wlr_drm_renderer {
	int fd;
	struct gbm_device *gbm;
	struct wlr_egl egl;

	struct wlr_renderer *wlr_rend;
};

struct wlr_drm_surface {
	struct wlr_drm_renderer *renderer;

	uint32_t width;
	uint32_t height;

	struct gbm_surface *gbm;
	EGLSurface egl;

	struct gbm_bo *front;
	struct gbm_bo *back;
};

bool wlr_drm_renderer_init(struct wlr_drm_backend *drm,
	struct wlr_drm_renderer *renderer);
void wlr_drm_renderer_finish(struct wlr_drm_renderer *renderer);

bool wlr_drm_surface_init(struct wlr_drm_surface *surf,
	struct wlr_drm_renderer *renderer, uint32_t width, uint32_t height,
	uint32_t format, uint32_t flags);

bool wlr_drm_plane_surfaces_init(struct wlr_drm_plane *plane,
	struct wlr_drm_backend *drm, int32_t width, uint32_t height,
	uint32_t format);

void wlr_drm_surface_finish(struct wlr_drm_surface *surf);
bool wlr_drm_surface_make_current(struct wlr_drm_surface *surf, int *buffer_age);
struct gbm_bo *wlr_drm_surface_swap_buffers(struct wlr_drm_surface *surf,
	pixman_region32_t *damage);
struct gbm_bo *wlr_drm_surface_get_front(struct wlr_drm_surface *surf);
void wlr_drm_surface_post(struct wlr_drm_surface *surf);
struct gbm_bo *wlr_drm_surface_mgpu_copy(struct wlr_drm_surface *dest,
	struct gbm_bo *src);

#endif
