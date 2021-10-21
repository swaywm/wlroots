#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wlr/render/timeline.h>
#include <wlr/types/wlr_linux_explicit_synchronization_v2.h>
#include <wlr/types/wlr_surface.h>
#include "linux-explicit-synchronization-v2-protocol.h"
#include "util/signal.h"

#define LINUX_EXPLICIT_SYNC_V2_VERSION 1

static const struct wp_linux_explicit_sync_v2_interface explicit_sync_impl;
static const struct wp_linux_sync_timeline_v2_interface sync_timeline_impl;
static const struct wp_linux_surface_sync_v2_interface surface_sync_impl;

static struct wlr_linux_explicit_sync_v2 *explicit_sync_from_resource(
		struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource,
		&wp_linux_explicit_sync_v2_interface, &explicit_sync_impl));
	return wl_resource_get_user_data(resource);
}

static struct wlr_render_timeline *sync_timeline_from_resource(
		struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource,
		&wp_linux_sync_timeline_v2_interface, &sync_timeline_impl));
	return wl_resource_get_user_data(resource);
}

// Returns NULL if the surface sync is inert
static struct wlr_linux_surface_sync_v2 *surface_sync_from_resource(
		struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource,
		&wp_linux_surface_sync_v2_interface, &surface_sync_impl));
	return wl_resource_get_user_data(resource);
}

static void sync_timeline_handle_resource_destroy(struct wl_resource *resource) {
	struct wlr_render_timeline *timeline = sync_timeline_from_resource(resource);
	// TODO: this causes use-after-free if the timeline is currently in use
	wlr_render_timeline_destroy(timeline);
}

static void sync_timeline_handle_destroy(struct wl_client *client,
		struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static const struct wp_linux_sync_timeline_v2_interface sync_timeline_impl = {
	.destroy = sync_timeline_handle_destroy,
};

static void surface_sync_destroy(
		struct wlr_linux_surface_sync_v2 *surface_sync) {
	if (surface_sync == NULL) {
		return;
	}
	wl_list_remove(&surface_sync->surface_commit.link);
	wlr_addon_finish(&surface_sync->addon);
	wl_resource_set_user_data(surface_sync->resource, NULL);
	free(surface_sync);
}

static void surface_sync_handle_destroy(struct wl_client *client,
		struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static void surface_sync_handle_set_acquire_point(struct wl_client *client,
		struct wl_resource *resource, struct wl_resource *timeline_resource,
		uint32_t point_hi, uint32_t point_lo) {
	struct wlr_linux_surface_sync_v2 *surface_sync =
		surface_sync_from_resource(resource);
	if (surface_sync == NULL) {
		wl_resource_post_error(resource,
			WP_LINUX_SURFACE_SYNC_V2_ERROR_NO_SURFACE,
			"The surface has been destroyed");
		return;
	}

	struct wlr_render_timeline *timeline =
		sync_timeline_from_resource(timeline_resource);
	uint64_t point = (uint64_t)point_hi << 32 | point_lo;

	if (surface_sync->pending.acquire_timeline != NULL) {
		wl_resource_post_error(resource,
			WP_LINUX_SURFACE_SYNC_V2_ERROR_DUPLICATE_ACQUIRE_POINT,
			"Acquire sync point already set");
		return;
	}

	surface_sync->pending.acquire_timeline = timeline;
	surface_sync->pending.acquire_point = point;
}

static void surface_sync_handle_set_release_point(struct wl_client *client,
		struct wl_resource *resource, struct wl_resource *timeline_resource,
		uint32_t point_hi, uint32_t point_lo) {
	struct wlr_linux_surface_sync_v2 *surface_sync =
		surface_sync_from_resource(resource);
	if (surface_sync == NULL) {
		wl_resource_post_error(resource,
			WP_LINUX_SURFACE_SYNC_V2_ERROR_NO_SURFACE,
			"The surface has been destroyed");
		return;
	}

	struct wlr_render_timeline *timeline =
		sync_timeline_from_resource(timeline_resource);
	uint64_t point = (uint64_t)point_hi << 32 | point_lo;

	if (surface_sync->pending.release_timeline != NULL) {
		wl_resource_post_error(resource,
			WP_LINUX_SURFACE_SYNC_V2_ERROR_DUPLICATE_RELEASE_POINT,
			"Release sync point already set");
		return;
	}

	surface_sync->pending.release_timeline = timeline;
	surface_sync->pending.release_point = point;
}

static const struct wp_linux_surface_sync_v2_interface surface_sync_impl = {
	.destroy = surface_sync_handle_destroy,
	.set_acquire_point = surface_sync_handle_set_acquire_point,
	.set_release_point = surface_sync_handle_set_release_point,
};

static void surface_sync_handle_resource_destroy(struct wl_resource *resource) {
	struct wlr_linux_surface_sync_v2 *surface_sync =
		surface_sync_from_resource(resource);
	surface_sync_destroy(surface_sync);
}

static void surface_sync_handle_surface_destroy(struct wlr_addon *addon) {
	struct wlr_linux_surface_sync_v2 *surface_sync =
		wl_container_of(addon, surface_sync, addon);
	surface_sync_destroy(surface_sync);
}

static const struct wlr_addon_interface addon_impl = {
	.name = "wp_linux_surface_sync_v2",
	.destroy = surface_sync_handle_surface_destroy,
};

static void explicit_sync_handle_destroy(struct wl_client *client,
		struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static struct wlr_linux_surface_sync_v2 *surface_sync_from_surface(
		struct wlr_linux_explicit_sync_v2 *explicit_sync,
		struct wlr_surface *surface) {
	struct wlr_addon *addon =
		wlr_addon_find(&surface->addons, explicit_sync, &addon_impl);
	if (addon == NULL) {
		return NULL;
	}
	struct wlr_linux_surface_sync_v2 *surface_sync =
		wl_container_of(addon, surface_sync, addon);
	return surface_sync;
}

static void surface_sync_handle_surface_commit(struct wl_listener *listener,
		void *data) {
	struct wlr_linux_surface_sync_v2 *surface_sync =
		wl_container_of(listener, surface_sync, surface_commit);

	if (surface_sync->pending.acquire_timeline != NULL &&
			surface_sync->surface->buffer == NULL) {
		wl_resource_post_error(surface_sync->resource,
			WP_LINUX_SURFACE_SYNC_V2_ERROR_NO_BUFFER,
			"Acquire point set but no buffer attached");
		return;
	}

	if (surface_sync->pending.release_timeline != NULL &&
			surface_sync->surface->buffer == NULL) {
		wl_resource_post_error(surface_sync->resource,
			WP_LINUX_SURFACE_SYNC_V2_ERROR_NO_BUFFER,
			"Release point set but no buffer attached");
		return;
	}

	// TODO: immediately signal current.release_timeline if necessary

	surface_sync->current = surface_sync->pending;
	memset(&surface_sync->pending, 0, sizeof(surface_sync->pending));
}

static void explicit_sync_handle_get_surface(struct wl_client *client,
		struct wl_resource *resource, uint32_t id,
		struct wl_resource *surface_resource) {
	struct wlr_linux_explicit_sync_v2 *explicit_sync =
		explicit_sync_from_resource(resource);
	struct wlr_surface *surface = wlr_surface_from_resource(surface_resource);

	if (surface_sync_from_surface(explicit_sync, surface) != NULL) {
		wl_resource_post_error(resource,
			WP_LINUX_EXPLICIT_SYNC_V2_ERROR_SURFACE_EXISTS,
			"wp_linux_surface_sync_v2 already created for this surface");
		return;
	}

	struct wlr_linux_surface_sync_v2 *surface_sync =
		calloc(1, sizeof(*surface_sync));
	if (surface_sync == NULL) {
		wl_resource_post_no_memory(resource);
		return;
	}

	uint32_t version = wl_resource_get_version(resource);
	surface_sync->resource = wl_resource_create(client,
		&wp_linux_surface_sync_v2_interface, version, id);
	if (surface_sync->resource == NULL) {
		wl_resource_post_no_memory(resource);
		free(surface_sync);
		return;
	}
	wl_resource_set_implementation(surface_sync->resource,
		&surface_sync_impl, surface_sync, surface_sync_handle_resource_destroy);

	surface_sync->surface = surface;

	surface_sync->surface_commit.notify = surface_sync_handle_surface_commit;
	wl_signal_add(&surface->events.commit, &surface_sync->surface_commit);

	wlr_addon_init(&surface_sync->addon, &surface->addons, explicit_sync,
		&addon_impl);
}

static void explicit_sync_handle_import_timeline(struct wl_client *client,
		struct wl_resource *resource, uint32_t id, int drm_syncobj_fd) {
	struct wlr_linux_explicit_sync_v2 *explicit_sync =
		explicit_sync_from_resource(resource);

	struct wlr_render_timeline *timeline =
		wlr_render_timeline_import(explicit_sync->drm_fd, drm_syncobj_fd);
	close(drm_syncobj_fd);
	if (timeline == NULL) {
		wl_resource_post_error(resource,
			WP_LINUX_EXPLICIT_SYNC_V2_ERROR_INVALID_TIMELINE,
			"Failed to import drm_syncobj timeline");
		return;
	}

	uint32_t version = wl_resource_get_version(resource);
	struct wl_resource *timeline_resource = wl_resource_create(client,
		&wp_linux_sync_timeline_v2_interface, version, id);
	if (timeline_resource == NULL) {
		wl_resource_post_no_memory(resource);
		return;
	}
	wl_resource_set_implementation(timeline_resource,
		&sync_timeline_impl, timeline, sync_timeline_handle_resource_destroy);
}

static const struct wp_linux_explicit_sync_v2_interface explicit_sync_impl = {
	.destroy = explicit_sync_handle_destroy,
	.get_surface = explicit_sync_handle_get_surface,
	.import_timeline = explicit_sync_handle_import_timeline,
};

static void explicit_sync_bind(struct wl_client *client, void *data,
		uint32_t version, uint32_t id) {
	struct wlr_linux_explicit_sync_v2 *explicit_sync = data;

	struct wl_resource *resource = wl_resource_create(client,
		&wp_linux_explicit_sync_v2_interface, version, id);
	if (resource == NULL) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(resource, &explicit_sync_impl,
		explicit_sync, NULL);
}

static void handle_display_destroy(struct wl_listener *listener, void *data) {
	struct wlr_linux_explicit_sync_v2 *explicit_sync =
		wl_container_of(listener, explicit_sync, display_destroy);
	wlr_signal_emit_safe(&explicit_sync->events.destroy, NULL);
	wl_list_remove(&explicit_sync->display_destroy.link);
	wl_global_destroy(explicit_sync->global);
	free(explicit_sync);
}

struct wlr_linux_explicit_sync_v2 *wlr_linux_explicit_sync_v2_create(
		struct wl_display *display, int drm_fd) {
	struct wlr_linux_explicit_sync_v2 *explicit_sync =
		calloc(1, sizeof(*explicit_sync));
	if (explicit_sync == NULL) {
		return NULL;
	}

	// TODO: maybe dup drm_fd here?
	explicit_sync->drm_fd = drm_fd;
	wl_signal_init(&explicit_sync->events.destroy);

	explicit_sync->global = wl_global_create(display,
		&wp_linux_explicit_sync_v2_interface, LINUX_EXPLICIT_SYNC_V2_VERSION,
		explicit_sync, explicit_sync_bind);
	if (explicit_sync->global == NULL) {
		free(explicit_sync);
		return NULL;
	}

	explicit_sync->display_destroy.notify = handle_display_destroy;
	wl_display_add_destroy_listener(display, &explicit_sync->display_destroy);

	return explicit_sync;
}

struct wlr_linux_surface_sync_v2_state *
wlr_linux_explicit_sync_v2_get_surface_state(
		struct wlr_linux_explicit_sync_v2 *explicit_sync,
		struct wlr_surface *surface) {
	struct wlr_linux_surface_sync_v2 *surface_sync =
		surface_sync_from_surface(explicit_sync, surface);
	if (surface_sync == NULL) {
		return NULL;
	}
	return &surface_sync->current;
}
