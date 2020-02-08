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
struct wlr_buffer;

struct wlr_drm_renderer {
	int fd;
	struct gbm_device *gbm;
	struct wlr_egl egl;

	uint32_t gbm_format;

	struct wlr_renderer *wlr_rend;
};

struct wlr_drm_surface {
	struct wlr_drm_renderer *renderer;

	uint32_t width;
	uint32_t height;

	struct gbm_surface *gbm;
	EGLSurface egl;
};

enum wlr_drm_fb_type {
	WLR_DRM_FB_TYPE_NONE,
	WLR_DRM_FB_TYPE_SURFACE,
	WLR_DRM_FB_TYPE_WLR_BUFFER
};

struct wlr_drm_fb {
	enum wlr_drm_fb_type type;
	struct gbm_bo *bo;

	struct wlr_drm_surface *mgpu_surf;
	struct gbm_bo *mgpu_bo;

	union {
		struct wlr_drm_surface *surf;
		struct wlr_buffer *wlr_buf;
	};
};

bool init_drm_renderer(struct wlr_drm_backend *drm,
	struct wlr_drm_renderer *renderer, wlr_renderer_create_func_t create_render);
void finish_drm_renderer(struct wlr_drm_renderer *renderer);

bool drm_surface_make_current(struct wlr_drm_surface *surf, int *buffer_age);
bool export_drm_bo(struct gbm_bo *bo, struct wlr_dmabuf_attributes *attribs);

void drm_fb_clear(struct wlr_drm_fb *fb);
bool drm_fb_lock_surface(struct wlr_drm_fb *fb, struct wlr_drm_surface *surf);
bool drm_fb_import_wlr(struct wlr_drm_fb *fb, struct wlr_drm_renderer *renderer,
		struct wlr_buffer *buf, struct wlr_drm_format_set *set);

void drm_fb_move(struct wlr_drm_fb *new, struct wlr_drm_fb *old);

bool drm_surface_render_black_frame(struct wlr_drm_surface *surf);
struct gbm_bo *drm_fb_acquire(struct wlr_drm_fb *fb, struct wlr_drm_backend *drm,
		struct wlr_drm_surface *mgpu);

bool drm_plane_init_surface(struct wlr_drm_plane *plane,
		struct wlr_drm_backend *drm, int32_t width, uint32_t height,
		uint32_t format, uint32_t flags, bool with_modifiers);
void drm_plane_finish_surface(struct wlr_drm_plane *plane);

#endif
