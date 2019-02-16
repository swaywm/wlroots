/*
 * This an unstable interface of wlroots. No guarantees are made regarding the
 * future consistency of this API.
 */
#ifndef WLR_USE_UNSTABLE
#error "Add -DWLR_USE_UNSTABLE to enable unstable wlroots features"
#endif

#ifndef WLR_TYPES_WLR_SURFACE_DAMAGE_H
#define WLR_TYPES_WLR_SURFACE_DAMAGE_H

#include <pixman.h>
#include <wayland-server.h>
#include <wlr/types/wlr_surface.h>

struct wlr_surface_damage;

struct wlr_surface_damage_interface {
	void (*destroy)(struct wlr_surface_damage *surface_damage);
	void (*add_children)(struct wlr_surface_damage *surface_damage);
};

void wlr_surface_damage_init(struct wlr_surface_damage *surface_damage,
	struct wlr_surface *surface,
	const struct wlr_surface_damage_interface *impl);
void wlr_surface_damage_add(struct wlr_surface_damage *surface_damage,
	int32_t sx, int32_t sy, pixman_region32_t *damage);

/**
 * Tracks damage coming from a surface and its children.
 *
 * When a surface maps, unmaps or commits, the `damage` event will be emitted.
 */
struct wlr_surface_damage {
	struct wlr_surface *surface;
	const struct wlr_surface_damage_interface *impl;

	struct {
		struct wl_signal destroy;
		struct wl_signal damage; // wlr_surface_damage_event
	} events;

	struct wl_list subsurfaces;

	struct wl_listener surface_destroy;
	struct wl_listener surface_commit;
	struct wl_listener surface_new_subsurface;

	void *data;
};

struct wlr_surface_damage_event {
	struct wlr_surface_damage *surface_damage;
	int32_t sx, sy;
	pixman_region32_t *damage;
};

/**
 * Tracks damage from a surface and its children subsurfaces.
 */
struct wlr_surface_damage *wlr_surface_damage_create(
	struct wlr_surface *surface);
void wlr_surface_damage_destroy(struct wlr_surface_damage *surface_damage);
/**
 * Damage the whole surface and all its children.
 */
void wlr_surface_damage_add_whole(struct wlr_surface_damage *surface_damage);

#endif
