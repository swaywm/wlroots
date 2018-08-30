#ifndef BACKEND_DRM_RENDERER_H
#define BACKEND_DRM_RENDERER_H

#include <EGL/egl.h>
#include <gbm.h>
#include <stdbool.h>
#include <stdint.h>
#include <wlr/backend.h>
#include <wlr/render/wlr_renderer.h>

struct wlr_drm_backend;
struct wlr_drm_plane;

struct wlr_drm_renderer {
	int fd;
	struct gbm_device *gbm;
	struct wlr_renderer *wlr_rend;
};

bool init_drm_renderer(struct wlr_drm_backend *drm,
	struct wlr_drm_renderer *renderer, wlr_renderer_create_func_t create_render);
void finish_drm_renderer(struct wlr_drm_renderer *renderer);

bool init_drm_render_surface(struct wlr_render_surface **surf,
		struct wlr_drm_renderer *renderer, uint32_t width, uint32_t height,
		uint32_t flags);
bool init_drm_plane_surfaces(struct wlr_drm_plane *plane,
	struct wlr_drm_backend *drm, int32_t width, uint32_t height);
struct gbm_bo *copy_drm_surface_mgpu(struct wlr_drm_renderer *renderer,
	struct wlr_render_surface *dest, struct gbm_bo *src);
bool export_drm_bo(struct gbm_bo *bo, struct wlr_dmabuf_attributes *attribs);

#endif
