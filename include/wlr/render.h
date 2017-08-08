#ifndef _WLR_RENDER_H
#define _WLR_RENDER_H
#include <stdint.h>
#include <wayland-server-protocol.h>
#include <wlr/types/wlr_output.h>

struct wlr_texture;
struct wlr_renderer;

void wlr_renderer_begin(struct wlr_renderer *r, struct wlr_output *output);
void wlr_renderer_end(struct wlr_renderer *r);
/**
 * Requests a texture handle from this renderer.
 */
struct wlr_texture *wlr_render_texture_init(struct wlr_renderer *r);
/**
 * Renders the requested texture using the provided matrix. A typical texture
 * rendering goes like so:
 *
 * 	struct wlr_renderer *renderer;
 * 	struct wlr_texture *texture;
 * 	float projection[16];
 * 	float matrix[16];
 * 	wlr_texture_get_matrix(texture, &matrix, &projection, 123, 321);
 * 	wlr_render_with_matrix(renderer, texture, &matrix);
 *
 * This will render the texture at <123, 321>.
 */
bool wlr_render_with_matrix(struct wlr_renderer *r,
		struct wlr_texture *texture, const float (*matrix)[16]);
/**
 * Renders a solid quad in the specified color.
 */
void wlr_render_colored_quad(struct wlr_renderer *r,
		const float (*color)[4], const float (*matrix)[16]);
/**
 * Renders a solid ellipse in the specified color.
 */
void wlr_render_colored_ellipse(struct wlr_renderer *r,
		const float (*color)[4], const float (*matrix)[16]);
/**
 * Returns a list of pixel formats supported by this renderer.
 */
const enum wl_shm_format *wlr_renderer_get_formats(
		struct wlr_renderer *r, size_t *len);
/**
 * Destroys this wlr_renderer. Textures must be destroyed separately.
 */
void wlr_renderer_destroy(struct wlr_renderer *renderer);

struct wlr_texture_impl;
struct wlr_texture_state;

struct wlr_texture {
	struct wlr_texture_impl *impl;
	struct wlr_texture_state *state;
	bool valid;
	uint32_t format;
	int width, height;
	struct wl_signal destroy_signal;
	struct wl_resource *resource;
};

/**
 * Uploads a pixel buffer to this texture. The buffer may be discarded after
 * calling this function.
 */
bool wlr_texture_upload_pixels(struct wlr_texture *surf,
		enum wl_shm_format format, int stride, int width, int height,
		const unsigned char *pixels);
/**
 * Uploads pixels from a wl_shm_buffer to this texture. The shm buffer may be
 * invalidated after calling this function.
 */
bool wlr_texture_upload_shm(struct wlr_texture *surf, uint32_t format,
		struct wl_shm_buffer *shm);
/**
 * Prepares a matrix with the appropriate scale for the given texture and
 * multiplies it with the projection, producing a matrix that the shader can
 * muptlipy vertex coordinates with to get final screen coordinates.
 *
 * The projection matrix is assumed to be an orthographic projection of [0,
 * width) and [0, height], and the x and y coordinates provided are used as
 * such.
 */
void wlr_texture_get_matrix(struct wlr_texture *texture,
		float (*matrix)[16], const float (*projection)[16], int x, int y);
/**
 * Destroys this wlr_texture.
 */
void wlr_texture_destroy(struct wlr_texture *texture);

#endif
