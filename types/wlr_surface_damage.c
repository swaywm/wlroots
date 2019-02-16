#include <assert.h>
#include <stdlib.h>
#include <wlr/types/wlr_surface_damage.h>
#include <wlr/util/log.h>
#include "util/signal.h"

struct subsurface_damage {
	struct wlr_surface_damage base;
	struct wlr_surface_damage *parent;
	struct wlr_subsurface *subsurface;
	struct wl_list link;

	struct wl_listener base_damage;
	struct wl_listener subsurface_destroy;
};

static const struct wlr_surface_damage_interface subsurface_damage_impl;

static void subsurface_damage_destroy(struct wlr_surface_damage *base) {
	assert(base->impl == &subsurface_damage_impl);
	struct subsurface_damage *subsurface_damage =
		(struct subsurface_damage *)base;
	wl_list_remove(&subsurface_damage->base_damage.link);
	wl_list_remove(&subsurface_damage->subsurface_destroy.link);
	wl_list_remove(&subsurface_damage->link);
	free(subsurface_damage);
}

static const struct wlr_surface_damage_interface subsurface_damage_impl = {
	.destroy = subsurface_damage_destroy,
};

static void handle_base_damage(struct wl_listener *listener, void *data) {
	struct subsurface_damage *subsurface_damage =
		wl_container_of(listener, subsurface_damage, base_damage);
	struct wlr_surface_damage_event *event = data;
	struct wlr_subsurface *subsurface = subsurface_damage->subsurface;

	if (subsurface->parent == NULL) {
		return;
	}

	int32_t sx = subsurface->current.x + event->sx;
	int32_t sy = subsurface->current.y + event->sy;
	wlr_surface_damage_add(subsurface_damage->parent, sx, sy, event->damage);
}

static void handle_subsurface_destroy(struct wl_listener *listener,
		void *data) {
	struct subsurface_damage *subsurface_damage =
		wl_container_of(listener, subsurface_damage, subsurface_destroy);
	wlr_surface_damage_destroy(&subsurface_damage->base);
}

struct subsurface_damage *subsurface_damage_create(
		struct wlr_surface_damage *parent, struct wlr_subsurface *subsurface) {
	struct subsurface_damage *subsurface_damage =
		calloc(1, sizeof(struct subsurface_damage));
	if (subsurface_damage == NULL) {
		return NULL;
	}

	wlr_surface_damage_init(&subsurface_damage->base,
		subsurface->surface, &subsurface_damage_impl);

	subsurface_damage->subsurface = subsurface;
	subsurface_damage->parent = parent;
	wl_list_insert(&parent->subsurfaces, &subsurface_damage->link);

	subsurface_damage->base_damage.notify = handle_base_damage;
	wl_signal_add(&subsurface_damage->base.events.damage,
		&subsurface_damage->base_damage);
	subsurface_damage->subsurface_destroy.notify = handle_subsurface_destroy;
	wl_signal_add(&subsurface->events.destroy,
		&subsurface_damage->subsurface_destroy);
	// TODO: map/unmap

	return subsurface_damage;
}


static void handle_surface_commit(struct wl_listener *listener, void *data) {
	struct wlr_surface_damage *surface_damage =
		wl_container_of(listener, surface_damage, surface_commit);

	if (!wlr_surface_has_buffer(surface_damage->surface)) {
		return;
	}

	pixman_region32_t damage;
	pixman_region32_init(&damage);
	wlr_surface_get_effective_damage(surface_damage->surface, &damage);
	wlr_surface_damage_add(surface_damage, 0, 0, &damage);
	pixman_region32_fini(&damage);
}

static void handle_surface_new_subsurface(struct wl_listener *listener,
		void *data) {
	struct wlr_surface_damage *surface_damage =
		wl_container_of(listener, surface_damage, surface_new_subsurface);
	struct wlr_subsurface *subsurface = data;

	subsurface_damage_create(surface_damage, subsurface);
}

static void handle_surface_destroy(struct wl_listener *listener, void *data) {
	struct wlr_surface_damage *surface_damage =
		wl_container_of(listener, surface_damage, surface_destroy);
	wlr_surface_damage_destroy(surface_damage);
}

void wlr_surface_damage_init(struct wlr_surface_damage *surface_damage,
		struct wlr_surface *surface,
		const struct wlr_surface_damage_interface *impl) {
	surface_damage->surface = surface;
	surface_damage->impl = impl;
	wl_list_init(&surface_damage->subsurfaces);
	wl_signal_init(&surface_damage->events.destroy);
	wl_signal_init(&surface_damage->events.damage);

	surface_damage->surface_destroy.notify = handle_surface_destroy;
	wl_signal_add(&surface->events.destroy, &surface_damage->surface_destroy);
	surface_damage->surface_commit.notify = handle_surface_commit;
	wl_signal_add(&surface->events.commit, &surface_damage->surface_commit);
	surface_damage->surface_new_subsurface.notify =
		handle_surface_new_subsurface;
	wl_signal_add(&surface->events.new_subsurface,
		&surface_damage->surface_new_subsurface);

	struct wlr_subsurface *subsurface;
	wl_list_for_each(subsurface, &surface->subsurfaces, parent_link) {
		subsurface_damage_create(surface_damage, subsurface);
	}
}

struct wlr_surface_damage *wlr_surface_damage_create(
		struct wlr_surface *surface) {
	struct wlr_surface_damage *surface_damage =
		calloc(1, sizeof(struct wlr_surface_damage));
	if (surface_damage == NULL) {
		return NULL;
	}
	wlr_surface_damage_init(surface_damage, surface, NULL);
	return surface_damage;
}

void wlr_surface_damage_destroy(struct wlr_surface_damage *surface_damage) {
	if (surface_damage == NULL) {
		return;
	}
	wlr_signal_emit_safe(&surface_damage->events.destroy, surface_damage);
	wl_list_remove(&surface_damage->surface_destroy.link);
	wl_list_remove(&surface_damage->surface_commit.link);
	wl_list_remove(&surface_damage->surface_new_subsurface.link);
	struct subsurface_damage *subsurface_damage, *tmp;
	wl_list_for_each_safe(subsurface_damage, tmp,
			&surface_damage->subsurfaces, link) {
		wlr_surface_damage_destroy(&subsurface_damage->base);
	}
	if (surface_damage->impl && surface_damage->impl->destroy) {
		surface_damage->impl->destroy(surface_damage);
	} else {
		free(surface_damage);
	}
}

void wlr_surface_damage_add(struct wlr_surface_damage *surface_damage,
		int32_t sx, int32_t sy, pixman_region32_t *damage) {
	struct wlr_surface_damage_event event = {
		.surface_damage = surface_damage,
		.sx = sx,
		.sy = sy,
		.damage = damage,
	};
	wlr_signal_emit_safe(&surface_damage->events.damage, &event);
}

void wlr_surface_damage_add_whole(struct wlr_surface_damage *surface_damage) {
	struct wlr_surface *surface = surface_damage->surface;

	pixman_region32_t damage;
	pixman_region32_init(&damage);
	pixman_region32_union_rect(&damage, &damage, 0, 0,
		surface->current.width, surface->current.height);
	wlr_surface_damage_add(surface_damage, surface->sx, surface->sy, &damage);
	pixman_region32_fini(&damage);

	struct subsurface_damage *subsurface_damage;
	wl_list_for_each(subsurface_damage, &surface_damage->subsurfaces, link) {
		wlr_surface_damage_add_whole(&subsurface_damage->base);
	}

	if (surface_damage->impl && surface_damage->impl->add_children) {
		surface_damage->impl->add_children(surface_damage);
	}
}
