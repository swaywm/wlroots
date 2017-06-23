#ifndef _WLR_RENDER_H
#define _WLR_RENDER_H
#include <stdint.h>
#include <wayland-server-protocol.h>
#include <wlr/types/wlr_output.h>

struct wlr_surface;
struct wlr_renderer;

void wlr_renderer_begin(struct wlr_renderer *r, struct wlr_output *output);
void wlr_renderer_end(struct wlr_renderer *r);
/**
 * Requests a surface handle from this renderer.
 */
struct wlr_surface *wlr_render_surface_init(struct wlr_renderer *r);
/**
 * Renders the requested surface using the provided matrix. A typical surface
 * rendering goes like so:
 *
 * 	struct wlr_renderer *renderer;
 * 	struct wlr_surface *surface;
 * 	float projection[16];
 * 	float matrix[16];
 * 	wlr_surface_get_matrix(surface, &matrix, &projection, 123, 321);
 * 	wlr_render_with_matrix(renderer, surface, &matrix);
 *
 * This will render the surface at <123, 321>.
 */
bool wlr_render_with_matrix(struct wlr_renderer *r,
		struct wlr_surface *surface, const float (*matrix)[16]);
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
 * Destroys this wlr_renderer. Surfaces must be destroyed separately.
 */
void wlr_renderer_destroy(struct wlr_renderer *renderer);

struct wlr_surface_impl;
struct wlr_surface_state;

struct wlr_surface {
	struct wlr_surface_impl *impl;
	struct wlr_surface_state *state;
	bool valid;
	uint32_t format;
	int width, height;
};

/**
 * Attaches a pixel buffer to this surface. The buffer may be discarded after
 * calling this function.
 */
bool wlr_surface_attach_pixels(struct wlr_surface *surf,
		enum wl_shm_format format, int width, int height,
		const unsigned char *pixels);
/**
 * Attaches pixels from a wl_shm_buffer to this surface. The shm buffer may be
 * invalidated after calling this function.
 */
bool wlr_surface_attach_shm(struct wlr_surface *surf, uint32_t format,
		struct wl_shm_buffer *shm);
/**
 * Prepares a matrix with the appropriate scale for the given surface and
 * multiplies it with the projection, producing a matrix that the shader can
 * muptlipy vertex coordinates with to get final screen coordinates.
 * 
 * The projection matrix is assumed to be an orthographic projection of [0,
 * width) and [0, height], and the x and y coordinates provided are used as
 * such.
 */
void wlr_surface_get_matrix(struct wlr_surface *surface,
		float (*matrix)[16], const float (*projection)[16], int x, int y);
/**
 * Destroys this wlr_surface.
 */
void wlr_surface_destroy(struct wlr_surface *tex);

#endif
