#ifndef WLR_RENDER_GLES2_H
#define WLR_RENDER_GLES2_H

#include <wlr/backend.h>
#include <wlr/render/wlr_renderer.h>

struct wlr_egl;

struct wlr_renderer *wlr_gles2_renderer_create(struct wlr_egl *egl);

#endif
