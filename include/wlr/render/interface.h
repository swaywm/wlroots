#ifndef WLR_RENDER_INTERFACE_H
#define WLR_RENDER_INTERFACE_H

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <stdbool.h>
#include <wayland-server-protocol.h>
#include <wlr/render.h>
#include <wlr/types/wlr_box.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_linux_dmabuf.h>

struct wlr_renderer_impl;

struct wlr_renderer {
	struct wlr_renderer_impl *impl;
};

struct wlr_renderer_impl {
	void (*begin)(struct wlr_renderer *renderer, struct wlr_output *output);
	void (*end)(struct wlr_renderer *renderer);
	void (*clear)(struct wlr_renderer *renderer, const float color[static 4]);
	void (*scissor)(struct wlr_renderer *renderer, struct wlr_box *box);
	struct wlr_texture *(*texture_create)(struct wlr_renderer *renderer);
	bool (*render_texture_with_matrix)(struct wlr_renderer *renderer,
		struct wlr_texture *texture, const float matrix[static 9],
		float alpha);
	void (*render_quad)(struct wlr_renderer *renderer,
		const float color[static 4], const float matrix[static 9]);
	void (*render_ellipse)(struct wlr_renderer *renderer,
		const float color[static 4], const float matrix[static 9]);
	const enum wl_shm_format *(*formats)(
		struct wlr_renderer *renderer, size_t *len);
	bool (*buffer_is_drm)(struct wlr_renderer *renderer,
		struct wl_resource *buffer);
	bool (*read_pixels)(struct wlr_renderer *renderer, enum wl_shm_format fmt,
		uint32_t stride, uint32_t width, uint32_t height,
		uint32_t src_x, uint32_t src_y, uint32_t dst_x, uint32_t dst_y,
		void *data);
	bool (*format_supported)(struct wlr_renderer *renderer,
		enum wl_shm_format fmt);
	void (*destroy)(struct wlr_renderer *renderer);
};

void wlr_renderer_init(struct wlr_renderer *renderer,
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
	bool (*upload_eglimage)(struct wlr_texture *texture, EGLImageKHR image,
		uint32_t width, uint32_t height);
	bool (*upload_dmabuf)(struct wlr_texture *texture,
		struct wl_resource *dmabuf_resource);
	void (*get_matrix)(struct wlr_texture *state, float mat[static 9],
		const float projection[static 9], int x, int y);
	void (*get_buffer_size)(struct wlr_texture *texture,
		struct wl_resource *resource, int *width, int *height);
	void (*bind)(struct wlr_texture *texture);
	void (*destroy)(struct wlr_texture *texture);
};

void wlr_texture_init(struct wlr_texture *texture,
		struct wlr_texture_impl *impl);
void wlr_texture_bind(struct wlr_texture *texture);
void wlr_texture_get_buffer_size(struct wlr_texture *texture,
		struct wl_resource *resource, int *width, int *height);

#endif
