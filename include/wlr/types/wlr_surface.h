#ifndef WLR_TYPES_WLR_SURFACE_H
#define WLR_TYPES_WLR_SURFACE_H

#include <pixman.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include <wayland-server.h>
#include <wlr/types/wlr_output.h>

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
#define WLR_SURFACE_INVALID_SUBSURFACE_POSITION 128
#define WLR_SURFACE_INVALID_FRAME_CALLBACK_LIST 256

struct wlr_surface_state {
	uint32_t invalid;
	struct wl_resource *buffer;
	struct wl_listener buffer_destroy_listener;
	int32_t sx, sy;
	pixman_region32_t surface_damage, buffer_damage;
	pixman_region32_t opaque, input;
	enum wl_output_transform transform;
	int32_t scale;
	int width, height;
	int buffer_width, buffer_height;

	struct {
		int32_t x, y;
	} subsurface_position;

	struct wl_list frame_callback_list; // wl_surface.frame
};

struct wlr_subsurface {
	struct wl_resource *resource;
	struct wlr_surface *surface;
	struct wlr_surface *parent;

	struct wlr_surface_state *cached;
	bool has_cache;

	bool synchronized;
	bool reordered;

	struct wl_list parent_link;
	struct wl_list parent_pending_link;

	struct wl_listener parent_destroy_listener;

	struct {
		struct wl_signal destroy;
	} events;
};

struct wlr_surface {
	struct wl_resource *resource;
	struct wlr_renderer *renderer;
	struct wlr_texture *texture;
	struct wlr_surface_state *current, *pending;
	const char *role; // the lifetime-bound role or null

	float buffer_to_surface_matrix[16];
	float surface_to_buffer_matrix[16];

	struct {
		struct wl_signal commit;
		struct wl_signal new_subsurface;
		struct wl_signal destroy;
	} events;

	// destroy listener used by compositor
	struct wl_listener compositor_listener;
	void *compositor_data;

	// surface commit callback for the role that runs before all others
	void (*role_committed)(struct wlr_surface *surface, void *role_data);
	void *role_data;

	// subsurface properties
	struct wlr_subsurface *subsurface;
	struct wl_list subsurface_list; // wlr_subsurface::parent_link

	// wlr_subsurface::parent_pending_link
	struct wl_list subsurface_pending_list;
	void *data;
};

struct wlr_renderer;
struct wlr_surface *wlr_surface_create(struct wl_resource *res,
		struct wlr_renderer *renderer);
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


/**
 * Set the lifetime role for this surface. Returns 0 on success or -1 if the
 * role cannot be set.
 */
int wlr_surface_set_role(struct wlr_surface *surface, const char *role,
		struct wl_resource *error_resource, uint32_t error_code);

/**
 * Whether or not this surface currently has an attached buffer. A surface has
 * an attached buffer when it commits with a non-null buffer in its pending
 * state. A surface will not have a buffer if it has never committed one, has
 * committed a null buffer, or something went wrong with uploading the buffer.
 */
bool wlr_surface_has_buffer(struct wlr_surface *surface);

/**
 * Create the subsurface implementation for this surface.
 */
void wlr_surface_make_subsurface(struct wlr_surface *surface,
		struct wlr_surface *parent, uint32_t id);

/**
 * Get the top of the subsurface tree for this surface.
 */
struct wlr_surface *wlr_surface_get_main_surface(struct wlr_surface *surface);

/**
 * Find a subsurface within this surface at the surface-local coordinates.
 * Returns the surface and coordinates in the topmost surface coordinate system
 * or NULL if no subsurface is found at that location.
 */
struct wlr_subsurface *wlr_surface_subsurface_at(struct wlr_surface *surface,
		double sx, double sy, double *sub_x, double *sub_y);

void wlr_surface_send_enter(struct wlr_surface *surface,
		struct wlr_output *output);

void wlr_surface_send_leave(struct wlr_surface *surface,
		struct wlr_output *output);

void wlr_surface_send_frame_done(struct wlr_surface *surface,
		const struct timespec *when);

/**
 * Set a callback for surface commit that runs before all the other callbacks.
 * This is intended for use by the surface role.
 */
void wlr_surface_set_role_committed(struct wlr_surface *surface,
		void (*role_committed)(struct wlr_surface *surface, void *role_data),
		void *role_data);

#endif
