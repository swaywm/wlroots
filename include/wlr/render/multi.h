#ifndef WLR_RENDER_MULTI_H
#define WLR_RENDER_MULTI_H

#include <wlr/backend.h>
#include <wlr/render/wlr_renderer.h>

struct wlr_renderer *wlr_multi_renderer_create();
void wlr_multi_renderer_add(struct wlr_renderer *renderer,
	struct wlr_renderer *child);
void wlr_multi_renderer_remove(struct wlr_renderer *renderer,
	struct wlr_renderer *child);
bool wlr_multi_renderer_is_empty(struct wlr_renderer *renderer);
bool wlr_renderer_is_multi(struct wlr_renderer *renderer);

struct wlr_texture *wlr_multi_texture_get_child(struct wlr_texture *texture,
	struct wlr_renderer *child_renderer);
bool wlr_multi_texture_is_empty(struct wlr_texture *texture);
bool wlr_texture_is_multi(struct wlr_texture *texture);

#endif
