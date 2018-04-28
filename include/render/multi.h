#ifndef RENDERER_MULTI_H
#define RENDERER_MULTI_H

#include <wayland-util.h>
#include <wlr/render/multi.h>

struct wlr_multi_renderer_child {
	struct wlr_renderer *renderer;
	struct wl_list link; // wlr_multi_renderer::children

	struct wl_listener destroy;
};

struct wlr_multi_renderer {
	struct wlr_renderer renderer;

	struct wl_list children; // wlr_multi_renderer_child::link
};

struct wlr_multi_texture_child {
	struct wlr_renderer *renderer;
	struct wlr_texture *texture;
	struct wl_list link; // wlr_multi_texture::children

	struct wl_listener destroy;
};

struct wlr_multi_texture {
	struct wlr_texture texture;
	int32_t width, height;

	struct wl_list children; // wlr_multi_texture_child::link
};

struct wlr_multi_texture *multi_texture_create();
void multi_texture_add(struct wlr_multi_texture *texture,
	struct wlr_texture *child, struct wlr_renderer *child_renderer);

#endif
