#ifndef WLR_RENDER_RENDERER_GLES_H
#define WLR_RENDER_RENDERER_GLES_H

struct wlr_backend;
struct wlr_renderer_2;

struct wlr_renderer_2 *wlr_gles_renderer_create(struct wlr_backend *backend);

#endif
