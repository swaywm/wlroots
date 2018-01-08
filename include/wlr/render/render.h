#ifndef WLR_RENDER_RENDER_H
#define WLR_RENDER_RENDER_H

#include <stdint.h>
#include <wayland-server.h>

/*
 * See wlr_backend_get_render().
 */

struct wlr_renderer;
struct wlr_texture;
struct wlr_output;

/*
 * Checks if fmt is supported.
 */
bool wlr_renderer_format_supported(enum wl_shm_format fmt);

/*
 * Configure the renderer to use the output dimensions and transformations.
 * output must be the current surface.
 */
void wlr_renderer_bind(struct wlr_renderer *rend, struct wlr_output *output);

/*
 * Clear the renderer surface to the color.
 */
void wlr_renderer_clear(struct wlr_renderer *rend, float r, float g, float b, float a);

/*
 * Render a sub-region of tex onto the surface.
 */
void wlr_renderer_render_subtexture(struct wlr_renderer *rend, struct wlr_texture *tex,
	int32_t tex_x1, int32_t tex_y1, int32_t tex_x2, int32_t tex_y2,
	int32_t pos_x1, int32_t pos_y1, int32_t pos_x2, int32_t pos_y2);

/*
 * Render tex onto the surface.
 */
void wlr_renderer_render_texture(struct wlr_renderer *rend, struct wlr_texture *tex,
	int32_t x1, int32_t y1, int32_t x2, int32_t y2);


/*
 * Renders tex onto the surface using a matrix.
 * See wlr_renderer_texture_with_matrix.
 */

void wlr_renderer_render_subtexture_with_matrix(struct wlr_renderer *rend, struct wlr_texture *tex,
	int32_t tex_x1, int32_t tex_y1, int32_t tex_x2, int32_t tex_y2,
	float matrix[static 9]);

/*
 * Renders tex onto the surface using a matrix.
 *
 * This happens in the OpenGL coordinate system (right handed).
 * The verticies are at (-1,-1), (-1, 1), (1, -1) and (1,1).
 *
 * The matrix should be row-major order.
 *
 * The currently bound transformation will NOT be applied as part of this render.
 */

void wlr_renderer_render_texture_with_matrix(struct wlr_renderer *rend, struct wlr_texture *tex,
	float matrix[static 9]);

/*
 * Render a colored rectangle onto the surface.
 */
void wlr_renderer_render_rect(struct wlr_renderer *rend, float r, float g, float b, float a,
	int32_t x1, int32_t y1, int32_t x2, int32_t y2);

/*
 * Render a colored ellipse onto the surface.
 */
void wlr_renderer_render_ellipse(struct wlr_renderer *rend, float r, float g, float b, float a,
	int32_t x1, int32_t y1, int32_t x2, int32_t y2);

/*
 * Read out of pixels of the currently bound surface into data.
 * stride is in bytes.
 */
bool wlr_renderer_read_pixels(struct wlr_renderer *rend, enum wl_shm_format wl_fmt,
	uint32_t stride, uint32_t width, uint32_t height,
	uint32_t src_x, uint32_t src_y, uint32_t dst_x, uint32_t dst_y,
	void *data);

/*
 * Create a new texture from raw pixel data.
 * stride is in bytes.
 * The returned texture is mutable.
 */
struct wlr_texture *wlr_texture_from_pixels(struct wlr_renderer *rend, enum wl_shm_format wl_fmt,
	uint32_t stride, uint32_t width, uint32_t height, const void *data);

/*
 * Create a new texture from a wayland DRM resources.
 * The returned texture is immutable.
 */
struct wlr_texture *wlr_texture_from_wl_drm(struct wlr_renderer *rend, struct wl_resource *data);

/*
 * Create a new texture from a DMA-BUF.
 * The returned texture is immutable.
 */
struct wlr_texture *wlr_texture_from_dmabuf(struct wlr_renderer *rend, uint32_t fourcc_fmt,
	uint32_t width, uint32_t height, int fd0, uint32_t offset0, uint32_t stride0);

/*
 * Get the texture width or height.
 */
int32_t wlr_texture_get_width(struct wlr_texture *tex);
int32_t wlr_texture_get_height(struct wlr_texture *tex);

/*
 * Update a texture with raw pixels.
 * tex must be mutable.
 */
bool wlr_texture_write_pixels(struct wlr_renderer *rend, struct wlr_texture *tex,
	enum wl_shm_format wl_fmt, uint32_t stride, uint32_t width, uint32_t height,
	uint32_t src_x, uint32_t src_y, uint32_t dst_x, uint32_t dst_y,
	const void *data);

/*
 * Free a texture.
 */
void wlr_texture_destroy(struct wlr_texture *tex);

#endif
