#include <stdbool.h>

#include <wayland-server.h>

#include <wlr/render/renderer.h>
#include <wlr/render/renderer/gles.h>
#include <wlr/render/renderer/interface.h>

struct wlr_renderer_2 *wlr_renderer_autocreate_2(struct wl_display *display,
		struct wlr_backend *backend) {
	return wlr_gles_renderer_create(display, backend);
}

void wlr_renderer_destroy_2(struct wlr_renderer_2 *renderer) {
	if (!renderer) {
		return;
	}

	renderer->impl->destroy(renderer);
}

struct wlr_allocator *wlr_renderer_get_allocator_2(struct wlr_renderer_2 *renderer) {
	return renderer->impl->get_allocator(renderer);
}

void wlr_renderer_bind_image_2(struct wlr_renderer_2 *renderer, struct wlr_image *img) {
	renderer->impl->bind_image(renderer, img);
}

void wlr_renderer_flush_2(struct wlr_renderer_2 *renderer, int *fence_out) {
	renderer->impl->flush(renderer, fence_out);
}

void wlr_renderer_clear_2(struct wlr_renderer_2 *renderer, const float color[static 4]) {
	renderer->impl->clear(renderer, color);
}

struct wlr_texture_2 *wlr_texture_from_buffer_2(struct wlr_renderer_2 *renderer,
		struct wl_resource *buffer) {
	return renderer->impl->texture_from_buffer(renderer, buffer);
}

void wlr_renderer_init_2(struct wlr_renderer_2 *renderer,
		const struct wlr_renderer_impl_2 *impl) {
	renderer->impl = impl;
	wl_signal_init(&renderer->events.destroy);
}

void wlr_texture_destroy_2(struct wlr_texture_2 *tex) {
	if (!tex) {
		return;
	}

	tex->impl->destroy(tex);
}

bool wlr_texture_apply_damage_2(struct wlr_texture_2 *tex, struct wl_resource *buffer,
		pixman_region32_t *damage) {
	return tex->impl->apply_damage(tex, buffer, damage);
}

void wlr_texture_init_2(struct wlr_texture_2 *tex,
		const struct wlr_texture_impl_2 *impl) {
	tex->impl = impl;
}
