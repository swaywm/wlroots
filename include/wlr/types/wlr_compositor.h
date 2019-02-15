/*
 * This an unstable interface of wlroots. No guarantees are made regarding the
 * future consistency of this API.
 */
#ifndef WLR_USE_UNSTABLE
#error "Add -DWLR_USE_UNSTABLE to enable unstable wlroots features"
#endif

#ifndef WLR_TYPES_WLR_COMPOSITOR_H
#define WLR_TYPES_WLR_COMPOSITOR_H

#include <wayland-server.h>
#include <wlr/render/wlr_renderer.h>

struct wlr_commit;
struct wlr_output;
struct wlr_surface;

struct wlr_subcompositor {
	struct wl_global *global;
};

struct wlr_compositor {
	struct wl_global *global;
	struct wlr_renderer *renderer;

	uint32_t ids;

	struct wlr_subcompositor subcompositor;

	struct {
		struct wl_signal new_surface;
		struct wl_signal new_surface_2;
		struct wl_signal new_state;
	} events;

	struct wl_listener display_destroy;
};

struct wlr_surface_2 {
	struct wl_resource *resource;
	struct wlr_compositor *compositor;

	struct {
		struct wl_signal commit;
		struct wl_signal destroy;
	} events;

	struct wlr_commit *pending;
	struct wl_list committed;

	struct wl_list frame_callbacks;
};

struct wlr_commit {
	struct wl_list link;
	struct wlr_surface_2 *surface;

	// If the user has called wl_surface.commit
	bool committed;
	// See wlr_commit_inhibit
	size_t inhibit;
	size_t ref_cnt;

	// wl_surface.attach
	struct wl_resource *buffer_resource;
	int32_t sx, sy;
	// wl_surface.damage
	pixman_region32_t surface_damage;
	// wl_surface.frame
	struct wl_list frame_callbacks;
	// wl_surface.set_opaque_region
	pixman_region32_t opaque_region;
	// wl_surface.set_input_region
	pixman_region32_t input_region;
	// wl_surface.set_buffer_transform
	enum wl_output_transform transform;
	// wl_surface.set_buffer_scale
	int32_t scale;
	// wl_surface.damage_buffer
	pixman_region32_t buffer_damage;

	size_t state_len;
	void **state;

	struct {
		struct wl_signal commit;
		struct wl_signal complete;
		struct wl_signal destroy;
	} events;
};

struct wlr_compositor_new_state_args {
	struct wlr_commit *old;
	struct wlr_commit *new;
};

struct wlr_compositor *wlr_compositor_create(struct wl_display *display,
	struct wlr_renderer *renderer);
uint32_t wlr_compositor_register(struct wlr_compositor *compositor);

struct wlr_surface_2 *wlr_surface_from_resource_2(struct wl_resource *resource);

struct wlr_commit *wlr_surface_get_commit(struct wlr_surface_2 *surface);
struct wlr_commit *wlr_surface_get_pending(struct wlr_surface_2 *surface);

void wlr_surface_send_enter_2(struct wlr_surface_2 *surf, struct wlr_output *output);
void wlr_surface_send_leave_2(struct wlr_surface_2 *surf, struct wlr_output *output);

void wlr_commit_unref(struct wlr_commit *commit);

void wlr_commit_inhibit(struct wlr_commit *commit);
void wlr_commit_uninhibit(struct wlr_commit *commit);

bool wlr_surface_is_subsurface(struct wlr_surface *surface);

/**
 * Get a subsurface from a surface. Can return NULL if the subsurface has been
 * destroyed.
 */
struct wlr_subsurface *wlr_subsurface_from_wlr_surface(
	struct wlr_surface *surface);

pixman_region32_t *wlr_region_from_resource(struct wl_resource *resource);

#endif
