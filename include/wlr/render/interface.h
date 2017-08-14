#ifndef _WLR_RENDER_INTERFACE_H
#define _WLR_RENDER_INTERFACE_H
#include <wayland-server-protocol.h>
#include <stdbool.h>
#include <wlr/render.h>
#include <wlr/types/wlr_output.h>

struct wlr_renderer_impl;
struct wlr_renderer_state;

struct wlr_renderer {
	struct wlr_renderer_impl *impl;
	struct wlr_renderer_state *state;
};

struct wlr_renderer_impl {
	void (*begin)(struct wlr_renderer_state *state, struct wlr_output *output);
	void (*end)(struct wlr_renderer_state *state);
	struct wlr_texture *(*texture_init)(struct wlr_renderer_state *state);
	bool (*render_with_matrix)(struct wlr_renderer_state *state,
		struct wlr_texture *texture, const float (*matrix)[16]);
	void (*render_quad)(struct wlr_renderer_state *state,
		const float (*color)[4], const float (*matrix)[16]);
	void (*render_ellipse)(struct wlr_renderer_state *state,
		const float (*color)[4], const float (*matrix)[16]);
	const enum wl_shm_format *(*formats)(
		struct wlr_renderer_state *state, size_t *len);
	bool (*buffer_is_drm)(struct wlr_renderer_state *state,
		struct wl_resource *buffer);
	void (*destroy)(struct wlr_renderer_state *state);
};

struct wlr_renderer *wlr_renderer_init(struct wlr_renderer_state *state,
		struct wlr_renderer_impl *impl);

struct wlr_texture_impl {
	bool (*upload_pixels)(struct wlr_texture *texture,
		enum wl_shm_format format, int stride, int width, int height,
		const unsigned char *pixels);
	bool (*update_pixels)(struct wlr_texture *texture,
		enum wl_shm_format format, int stride, int x, int y,
		int width, int height, const unsigned char *pixels);
	bool (*upload_shm)(struct wlr_texture *texture, uint32_t format,
		struct wl_shm_buffer *shm);
	bool (*update_shm)(struct wlr_texture *texture, uint32_t format,
		int x, int y, int width, int height, struct wl_shm_buffer *shm);
	bool (*upload_drm)(struct wlr_texture *texture,
		struct wl_resource *drm_buf);
	void (*get_matrix)(struct wlr_texture *state,
		float (*matrix)[16], const float (*projection)[16], int x, int y);
	void (*bind)(struct wlr_texture *texture);
	void (*destroy)(struct wlr_texture *texture);
};

void wlr_texture_init(struct wlr_texture *texture,
		struct wlr_texture_impl *impl);
void wlr_texture_bind(struct wlr_texture *texture);

#endif
