#ifndef WLR_RENDER_RENDERER_INTERFACE_H
#define WLR_RENDER_RENDERER_INTERFACE_H

/*
 * New API.
 * TODO: Remove the _2 suffix when the old API is removed.
 */

struct wlr_allocator;
struct wlr_image;
struct wlr_renderer_2;

struct wlr_renderer_impl_2 {
	void (*destroy)(struct wlr_renderer_2 *renderer);

	struct wlr_allocator *(*get_allocator)(struct wlr_renderer_2 *renderer);

	void (*bind_image)(struct wlr_renderer_2 *renderer, struct wlr_image *img);
	void (*flush)(struct wlr_renderer_2 *renderer, int *fence_out);

	void (*clear)(struct wlr_renderer_2 *renderer, const float color[static 4]);
};

void wlr_renderer_init_2(struct wlr_renderer_2 *renderer,
		const struct wlr_renderer_impl_2 *impl);

#endif
