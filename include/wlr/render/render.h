#ifndef WLR_RENDER_RENDER_H
#define WLR_RENDER_RENDER_H

#include <stdint.h>
#include <wayland-server.h>

/*
 * See wlr_backend_get_render().
 */

struct wlr_render;
struct wlr_tex;
struct wlr_output;

/*
 * Checks if fmt is supported.
 */
bool wlr_render_format_supported(enum wl_shm_format fmt);

/*
 * Configure the renderer to use the output dimensions and transformations.
 * output must be the current surface.
 */
void wlr_render_bind(struct wlr_render *rend, struct wlr_output *output);

/*
 * Returns the currently bound 3x3 transform matrix.
 * The returned matrix will have 9 elements and is in row-major order.
 */
const float *wlr_render_get_transform(struct wlr_render *rend);

/*
 * Clear the renderer surface to the color.
 */
void wlr_render_clear(struct wlr_render *rend, float r, float g, float b, float a);

/*
 * Render a sub-region of tex onto the surface.
 */
void wlr_render_subtexture(struct wlr_render *rend, struct wlr_tex *tex,
	int32_t tex_x1, int32_t tex_y1, int32_t tex_x2, int32_t tex_y2,
	int32_t pos_x1, int32_t pos_y1, int32_t pos_x2, int32_t pos_y2, int32_t pos_z);

/*
 * Render tex onto the surface.
 */
void wlr_render_texture(struct wlr_render *rend, struct wlr_tex *tex,
	int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t z);


/*
 * Renders tex onto the surface using a matrix.
 * See wlr_render_texture_with_matrix.
 */

void wlr_render_subtexture_with_matrix(struct wlr_render *rend, struct wlr_tex *tex,
	int32_t tex_x1, int32_t tex_y1, int32_t tex_x2, int32_t tex_y2,
	float matrix[static 9], int32_t pos_z);

/*
 * Renders tex onto the surface using a matrix.
 *
 * This happens in the OpenGL coordinate system (right handed).
 * The verticies are at (-1,-1), (-1, 1), (1, -1) and (1,1).
 *
 * The matrix should be row-major order.
 *
 * The currently bound transformation will NOT be applied as part of this render.
 * You should use wlr_render_get_transform, and multiply it into your own matrix.
 */

void wlr_render_texture_with_matrix(struct wlr_render *rend, struct wlr_tex *tex,
	float matrix[static 9], int32_t pos_z);

/*
 * Render a colored rectangle onto the surface.
 */
void wlr_render_rect(struct wlr_render *rend, float r, float g, float b, float a,
	int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t z);

/*
 * Render a colored ellipse onto the surface.
 */
void wlr_render_ellipse(struct wlr_render *rend, float r, float g, float b, float a,
	int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t z);

/*
 * Read out of pixels of the currently bound surface into data.
 * stride is in bytes.
 */
bool wlr_render_read_pixels(struct wlr_render *rend, enum wl_shm_format wl_fmt,
	uint32_t stride, uint32_t width, uint32_t height,
	uint32_t src_x, uint32_t src_y, uint32_t dst_x, uint32_t dst_y,
	void *data);

/*
 * Create a new texture from raw pixel data.
 * stride in in bytes.
 * The returned texture is mutable.
 */
struct wlr_tex *wlr_tex_from_pixels(struct wlr_render *rend, enum wl_shm_format wl_fmt,
	uint32_t stride, uint32_t width, uint32_t height, const void *data);

/*
 * Create a new texture from a wayland DRM resources.
 * The returned texture is immutable.
 */
struct wlr_tex *wlr_tex_from_wl_drm(struct wlr_render *rend, struct wl_resource *data);

/*
 * Create a new texture from a DMA-BUF.
 * The returned texture is immutable.
 */
struct wlr_tex *wlr_tex_from_dmabuf(struct wlr_render *rend, uint32_t fourcc_fmt,
	uint32_t width, uint32_t height, int fd0, uint32_t offset0, uint32_t stride0);

/*
 * Get the texture width or height.
 */
int32_t wlr_tex_get_width(struct wlr_tex *tex);
int32_t wlr_tex_get_height(struct wlr_tex *tex);

/*
 * Update a texture with raw pixels.
 * tex must be mutable.
 */
bool wlr_tex_write_pixels(struct wlr_render *rend, struct wlr_tex *tex,
	enum wl_shm_format wl_fmt, uint32_t stride, uint32_t width, uint32_t height,
	uint32_t src_x, uint32_t src_y, uint32_t dst_x, uint32_t dst_y,
	const void *data);

/*
 * Free a texture.
 */
void wlr_tex_destroy(struct wlr_tex *tex);

#endif
