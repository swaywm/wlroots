#ifndef _WLR_GLES2_RENDERER_H
#define _WLR_GLES2_RENDERER_H
#include <wlr/render.h>
#include <wlr/backend.h>

struct wlr_egl;
struct wlr_renderer *wlr_gles2_renderer_create(struct wlr_backend *backend);

#endif
