#ifndef _WLR_TYPES_WLR_SURFACE_H
#define _WLR_TYPES_WLR_SURFACE_H
#include <wayland-server.h>
#include <pixman.h>
#include <stdint.h>

struct wlr_frame_callback {
	struct wl_resource *resource;
	struct wl_list link;
};

#define WLR_SURFACE_INVALID_BUFFER 1
#define WLR_SURFACE_INVALID_SURFACE_DAMAGE 2
#define WLR_SURFACE_INVALID_BUFFER_DAMAGE 4
#define WLR_SURFACE_INVALID_OPAQUE_REGION 8
#define WLR_SURFACE_INVALID_INPUT_REGION 16
#define WLR_SURFACE_INVALID_TRANSFORM 32
#define WLR_SURFACE_INVALID_SCALE 64

struct wlr_surface_state {
	uint32_t invalid;
	struct wl_resource *buffer;
	int32_t sx, sy;
	pixman_region32_t surface_damage, buffer_damage;
	pixman_region32_t opaque, input;
	enum wl_output_transform transform;
	int32_t scale;
	int width, height;
	int buffer_width, buffer_height;
};

struct wlr_surface {
	struct wl_resource *resource;
	struct wlr_renderer *renderer;
	struct wlr_texture *texture;
	struct wlr_surface_state current, pending;
	const char *role; // the lifetime-bound role or null

	float buffer_to_surface_matrix[16];
	float surface_to_buffer_matrix[16];
	bool reupload_buffer;

	struct {
		struct wl_signal commit;
		struct wl_signal destroy;
	} signals;

	struct wl_list frame_callback_list; // wl_surface.frame

	struct wl_listener compositor_listener; // destroy listener used by compositor
	void *compositor_data;

	void *data;
};

struct wlr_renderer;
struct wlr_surface *wlr_surface_create(struct wl_resource *res,
		struct wlr_renderer *renderer);
void wlr_surface_flush_damage(struct wlr_surface *surface);
/**
 * Gets a matrix you can pass into wlr_render_with_matrix to display this
 * surface. `matrix` is the output matrix, `projection` is the wlr_output
 * projection matrix, and `transform` is any additional transformations you want
 * to perform on the surface (or NULL/the identity matrix if you don't).
 * `transform` is used before the surface is scaled, so its geometry extends
 * from 0 to 1 in both dimensions.
 */
void wlr_surface_get_matrix(struct wlr_surface *surface,
		float (*matrix)[16],
		const float (*projection)[16],
		const float (*transform)[16]);

#endif
