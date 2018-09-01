#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <wlr/render/wlr_render_surface.h>
#include <wlr/render/interface.h>

struct wlr_render_surface *wlr_render_surface_create_headless(
		struct wlr_renderer *renderer, uint32_t width, uint32_t height) {
	if (!renderer->impl->render_surface_create_headless) {
		return NULL;
	}

	return renderer->impl->render_surface_create_headless(renderer,
		width, height);
}

struct wlr_render_surface *wlr_render_surface_create_gbm(
		struct wlr_renderer *renderer, uint32_t width, uint32_t height,
		void *gbm_device, uint32_t gbm_use_flags) {
	if (!renderer->impl->render_surface_create_gbm) {
		return NULL;
	}

	return renderer->impl->render_surface_create_gbm(renderer, width, height,
		gbm_device, gbm_use_flags);
}

struct wlr_render_surface *wlr_render_surface_create_xcb(
		struct wlr_renderer *renderer, uint32_t width, uint32_t height,
		void *xcb_connection, uint32_t window) {
	if (!renderer->impl->render_surface_create_xcb) {
		return NULL;
	}

	return renderer->impl->render_surface_create_xcb(renderer, width, height,
		xcb_connection, window);
}

struct wlr_render_surface *wlr_render_surface_create_wl(
		struct wlr_renderer *renderer, uint32_t width, uint32_t height,
		struct wl_display *disp, struct wl_surface *surf) {
	if (!renderer->impl->render_surface_create_wl) {
		return NULL;
	}

	return renderer->impl->render_surface_create_wl(renderer, width, height,
		disp, surf);
}

void wlr_render_surface_init(struct wlr_render_surface *surface,
		const struct wlr_render_surface_impl *impl) {
	assert(impl->resize);
	surface->impl = impl;
}

int wlr_render_surface_get_buffer_age(struct wlr_render_surface *surface) {
	if (!surface->impl->buffer_age) {
		return -1;
	}

	return surface->impl->buffer_age(surface);
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

struct gbm_bo* wlr_render_surface_get_bo(struct wlr_render_surface* surface) {
	if (!surface->impl->get_bo) {
		return NULL;
	}

	return surface->impl->get_bo(surface);
}

bool wlr_render_surface_read_pixels(struct wlr_render_surface *r,
		enum wl_shm_format fmt, uint32_t *flags, uint32_t stride,
		uint32_t width, uint32_t height, uint32_t src_x, uint32_t src_y,
		uint32_t dst_x, uint32_t dst_y, void *data) {
	if (!r->impl->read_pixels) {
		return false;
	}
	return r->impl->read_pixels(r, fmt, flags, stride, width, height,
		src_x, src_y, dst_x, dst_y, data);
}


