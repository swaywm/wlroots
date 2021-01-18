/*
 * This an unstable interface of wlroots. No guarantees are made regarding the
 * future consistency of this API.
 */
#ifndef WLR_USE_UNSTABLE
#error "Add -DWLR_USE_UNSTABLE to enable unstable wlroots features"
#endif

#ifndef WLR_RENDER_PIXMAN_H
#define WLR_RENDER_PIXMAN_H

#include <wlr/backend.h>
#include <wlr/render/wlr_renderer.h>

struct wlr_renderer *wlr_pixman_renderer_create(void);

#endif
