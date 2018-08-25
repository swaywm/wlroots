#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <wlr/render/wlr_render_surface.h>
#include <wlr/render/interface.h>

void wlr_render_surface_init(struct wlr_render_surface *surface,
		const struct wlr_render_surface_impl *impl) {
	assert(impl->resize);
	surface->impl = impl;
}

void wlr_render_surface_destroy(struct wlr_render_surface *surface) {
	if (!surface) {
		return;
	}

	if (surface->impl && surface->impl->destroy) {
		surface->impl->destroy(surface);
	} else {
		free(surface);
	}
}

void wlr_render_surface_resize(struct wlr_render_surface *surface,
		uint32_t width, uint32_t height) {
	surface->impl->resize(surface, width, height);
}

bool wlr_render_surface_swap_buffers(struct wlr_render_surface *surface,
		pixman_region32_t *damage) {
	if (surface->impl->swap_buffers) {
		return surface->impl->swap_buffers(surface, damage);
	}

	return true;
}

