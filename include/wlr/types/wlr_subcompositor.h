#ifndef WLR_TYPES_WLR_SUBCOMPOSITOR_H
#define WLR_TYPES_WLR_SUBCOMPOSITOR_H

#include <stdbool.h>
#include <stdint.h>
#include <wayland-server.h>

struct wlr_compositor;
struct wlr_commit;
struct wlr_surface_2;

struct wlr_subcompositor {
	struct wl_global *global;
	struct wlr_compositor *compositor;

	uint32_t root_id;

	struct {
		struct wl_signal new_subsurface;
	} events;

	struct wl_listener new_state;
	struct wl_listener display_destroy;
};

struct wlr_subsurface {
	struct wl_resource *resource;
	struct wlr_surface_2 *surface;
	struct wlr_surface_2 *parent;

	bool synchronized;

	struct {
		struct wl_signal destroy;
	} events;

	struct wl_listener surface_destroy;
	struct wl_listener parent_destroy;
};

struct wlr_subcompositor *wlr_subcompositor_create(struct wlr_compositor *compositor);

struct wlr_subsurface *wlr_surface_to_subsurface(struct wlr_surface_2 *surf);
struct wlr_surface_2 *wlr_surface_get_root_surface(struct wlr_surface_2 *surf);

typedef void (*wlr_subsurface_iter_t)(void *userdata,
	struct wlr_commit *commit, int32_t x, int32_t y);

void wlr_commit_for_each_subsurface(struct wlr_subcompositor *sc,
		struct wlr_commit *commit, wlr_subsurface_iter_t func, void *userdata);

#endif
