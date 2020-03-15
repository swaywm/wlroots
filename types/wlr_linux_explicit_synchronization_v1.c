#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wlr/render/timeline.h>
#include <wlr/types/wlr_linux_explicit_synchronization_v1.h>
#include <wlr/types/wlr_surface.h>
#include "linux-explicit-synchronization-unstable-v1-protocol.h"
#include "util/signal.h"

#define LINUX_EXPLICIT_SYNCHRONIZATION_V1_VERSION 2

static const struct zwp_linux_explicit_synchronization_v1_interface
	explicit_sync_impl;
static const struct zwp_linux_surface_synchronization_v1_interface
	surface_sync_impl;

static struct wlr_linux_explicit_synchronization_v1 *
explicit_sync_from_resource(struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource,
		&zwp_linux_explicit_synchronization_v1_interface,
		&explicit_sync_impl));
	return wl_resource_get_user_data(resource);
}

// Returns NULL if the surface sync is inert
static struct wlr_linux_surface_synchronization_v1 *
surface_sync_from_resource(struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource,
		&zwp_linux_surface_synchronization_v1_interface,
		&surface_sync_impl));
	return wl_resource_get_user_data(resource);
}

static struct wlr_linux_buffer_release_v1 *buffer_release_from_resource(
		struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource,
		&zwp_linux_buffer_release_v1_interface, NULL));
	return wl_resource_get_user_data(resource);
}

static void buffer_release_destroy(
		struct wlr_linux_buffer_release_v1 *buffer_release, int fence_fd) {
	if (buffer_release == NULL) {
		return;
	}
	if (fence_fd >= 0) {
		zwp_linux_buffer_release_v1_send_fenced_release(
			buffer_release->resource, fence_fd);
	} else {
		zwp_linux_buffer_release_v1_send_immediate_release(
			buffer_release->resource);
	}
	wl_resource_destroy(buffer_release->resource);
}

static void buffer_release_handle_resource_destroy(struct wl_resource *resource) {
	struct wlr_linux_buffer_release_v1 *buffer_release =
		buffer_release_from_resource(resource);
	free(buffer_release);
}

static void surface_sync_handle_destroy(struct wl_client *client,
		struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static void surface_sync_handle_set_acquire_fence(struct wl_client *client,
		struct wl_resource *resource, int fence_fd) {
	struct wlr_linux_surface_synchronization_v1 *surface_sync =
		surface_sync_from_resource(resource);
	if (surface_sync == NULL) {
		close(fence_fd);
		wl_resource_post_error(resource,
			ZWP_LINUX_SURFACE_SYNCHRONIZATION_V1_ERROR_NO_SURFACE,
			"the surface has been destroyed");
		return;
	}

	if (surface_sync->pending.acquire_fence_fd >= 0) {
		close(fence_fd);
		wl_resource_post_error(resource,
			ZWP_LINUX_SURFACE_SYNCHRONIZATION_V1_ERROR_DUPLICATE_FENCE,
			"a fence FD was already set for this commit");
		return;
	}

	// TODO: check that the FD is a sync_file

	surface_sync->pending.acquire_fence_fd = fence_fd;
}

static void surface_sync_handle_get_release(struct wl_client *client,
		struct wl_resource *resource, uint32_t id) {
	struct wlr_linux_surface_synchronization_v1 *surface_sync =
		surface_sync_from_resource(resource);
	if (surface_sync == NULL) {
		wl_resource_post_error(resource,
			ZWP_LINUX_SURFACE_SYNCHRONIZATION_V1_ERROR_NO_SURFACE,
			"the surface has been destroyed");
		return;
	}

	if (surface_sync->pending.buffer_release != NULL) {
		wl_resource_post_error(resource,
			ZWP_LINUX_SURFACE_SYNCHRONIZATION_V1_ERROR_DUPLICATE_RELEASE,
			"a buffer release has already been requested for this commit");
		return;
	}

	struct wlr_linux_buffer_release_v1 *buffer_release =
		calloc(1, sizeof(*buffer_release));
	if (buffer_release == NULL) {
		wl_resource_post_no_memory(resource);
		return;
	}

	uint32_t version = wl_resource_get_version(resource);
	buffer_release->resource = wl_resource_create(client,
		&zwp_linux_buffer_release_v1_interface, version, id);
	if (buffer_release->resource == NULL) {
		wl_resource_post_no_memory(resource);
		free(buffer_release);
		return;
	}
	wl_resource_set_implementation(buffer_release->resource, NULL,
		buffer_release, buffer_release_handle_resource_destroy);

	surface_sync->pending.buffer_release = buffer_release;
}

static const struct zwp_linux_surface_synchronization_v1_interface
		surface_sync_impl = {
	.destroy = surface_sync_handle_destroy,
	.set_acquire_fence = surface_sync_handle_set_acquire_fence,
	.get_release = surface_sync_handle_get_release,
};

static void surface_sync_state_init(
		struct wlr_linux_surface_synchronization_v1_state *state) {
	memset(state, 0, sizeof(*state));
	state->acquire_fence_fd = -1;
}

static void surface_sync_state_finish(
		struct wlr_linux_surface_synchronization_v1_state *state) {
	if (state->acquire_fence_fd >= 0) {
		close(state->acquire_fence_fd);
	}
	buffer_release_destroy(state->buffer_release, -1);
}

static void surface_sync_destroy(
		struct wlr_linux_surface_synchronization_v1 *surface_sync) {
	if (surface_sync == NULL) {
		return;
	}
	wl_list_remove(&surface_sync->surface_commit.link);
	wlr_addon_finish(&surface_sync->addon);
	wl_resource_set_user_data(surface_sync->resource, NULL);
	surface_sync_state_finish(&surface_sync->pending);
	surface_sync_state_finish(&surface_sync->current);
	free(surface_sync);
}

static void surface_sync_handle_surface_commit(struct wl_listener *listener,
		void *data) {
	struct wlr_linux_surface_synchronization_v1 *surface_sync =
		wl_container_of(listener, surface_sync, surface_commit);

	if (surface_sync->pending.acquire_fence_fd >= 0 &&
			surface_sync->surface->buffer == NULL) {
		wl_resource_post_error(surface_sync->resource,
			ZWP_LINUX_SURFACE_SYNCHRONIZATION_V1_ERROR_NO_BUFFER,
			"acquire fence FD set but no buffer attached");
		return;
	}

	if (surface_sync->pending.buffer_release != NULL &&
			surface_sync->surface->buffer == NULL) {
		wl_resource_post_error(surface_sync->resource,
			ZWP_LINUX_SURFACE_SYNCHRONIZATION_V1_ERROR_NO_BUFFER,
			"buffer release requested but no buffer attached");
		return;
	}

	surface_sync_state_finish(&surface_sync->current);
	surface_sync->current = surface_sync->pending;
	surface_sync_state_init(&surface_sync->pending);
}

static void surface_sync_handle_resource_destroy(struct wl_resource *resource) {
	struct wlr_linux_surface_synchronization_v1 *surface_sync =
		surface_sync_from_resource(resource);
	surface_sync_destroy(surface_sync);
}

static void surface_sync_handle_surface_destroy(struct wlr_addon *addon) {
	struct wlr_linux_surface_synchronization_v1 *surface_sync =
		wl_container_of(addon, surface_sync, addon);
	surface_sync_destroy(surface_sync);
}

static const struct wlr_addon_interface addon_impl = {
	.name = "zwp_linux_surface_synchronization_v1",
	.destroy = surface_sync_handle_surface_destroy,
};

static void explicit_sync_handle_destroy(struct wl_client *client,
		struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static struct wlr_linux_surface_synchronization_v1 *surface_sync_from_surface(
		struct wlr_linux_explicit_synchronization_v1 *explicit_sync,
		struct wlr_surface *surface) {
	struct wlr_addon *addon =
		wlr_addon_find(&surface->addons, explicit_sync, &addon_impl);
	if (addon == NULL) {
		return NULL;
	}
	struct wlr_linux_surface_synchronization_v1 *surface_sync =
		wl_container_of(addon, surface_sync, addon);
	return surface_sync;
}

static void explicit_sync_handle_get_synchronization(struct wl_client *client,
		struct wl_resource *resource, uint32_t id,
		struct wl_resource *surface_resource) {
	struct wlr_linux_explicit_synchronization_v1 *explicit_sync =
		explicit_sync_from_resource(resource);
	struct wlr_surface *surface = wlr_surface_from_resource(surface_resource);

	if (surface_sync_from_surface(explicit_sync, surface) != NULL) {
		wl_resource_post_error(resource,
			ZWP_LINUX_EXPLICIT_SYNCHRONIZATION_V1_ERROR_SYNCHRONIZATION_EXISTS,
			"zwp_linux_surface_synchronization_v1 already created for this surface");
		return;
	}

	struct wlr_linux_surface_synchronization_v1 *surface_sync =
		calloc(1, sizeof(*surface_sync));
	if (surface_sync == NULL) {
		wl_resource_post_no_memory(resource);
		return;
	}

	uint32_t version = wl_resource_get_version(resource);
	surface_sync->resource = wl_resource_create(client,
		&zwp_linux_surface_synchronization_v1_interface, version, id);
	if (surface_sync->resource == NULL) {
		wl_resource_post_no_memory(resource);
		free(surface_sync);
		return;
	}
	wl_resource_set_implementation(surface_sync->resource,
		&surface_sync_impl, surface_sync, surface_sync_handle_resource_destroy);

	surface_sync->surface = surface;
	surface_sync_state_init(&surface_sync->pending);
	surface_sync_state_init(&surface_sync->current);

	surface_sync->surface_commit.notify = surface_sync_handle_surface_commit;
	wl_signal_add(&surface->events.commit, &surface_sync->surface_commit);

	wlr_addon_init(&surface_sync->addon, &surface->addons, explicit_sync,
		&addon_impl);
}

static const struct zwp_linux_explicit_synchronization_v1_interface
		explicit_sync_impl = {
	.destroy = explicit_sync_handle_destroy,
	.get_synchronization = explicit_sync_handle_get_synchronization,
};

static void explicit_sync_bind(struct wl_client *client, void *data,
		uint32_t version, uint32_t id) {
	struct wlr_linux_explicit_synchronization_v1 *explicit_sync = data;

	struct wl_resource *resource = wl_resource_create(client,
		&zwp_linux_explicit_synchronization_v1_interface, version, id);
	if (resource == NULL) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(resource, &explicit_sync_impl,
		explicit_sync, NULL);
}

static void handle_display_destroy(struct wl_listener *listener, void *data) {
	struct wlr_linux_explicit_synchronization_v1 *explicit_sync =
		wl_container_of(listener, explicit_sync, display_destroy);
	wlr_signal_emit_safe(&explicit_sync->events.destroy, NULL);
	wl_list_remove(&explicit_sync->display_destroy.link);
	wl_global_destroy(explicit_sync->global);
	free(explicit_sync);
}

struct wlr_linux_explicit_synchronization_v1 *
wlr_linux_explicit_synchronization_v1_create(struct wl_display *display) {
	struct wlr_linux_explicit_synchronization_v1 *explicit_sync =
		calloc(1, sizeof(*explicit_sync));
	if (explicit_sync == NULL) {
		return NULL;
	}

	wl_signal_init(&explicit_sync->events.destroy);

	explicit_sync->global = wl_global_create(display,
		&zwp_linux_explicit_synchronization_v1_interface,
		LINUX_EXPLICIT_SYNCHRONIZATION_V1_VERSION, explicit_sync,
		explicit_sync_bind);
	if (explicit_sync->global == NULL) {
		free(explicit_sync);
		return NULL;
	}

	explicit_sync->display_destroy.notify = handle_display_destroy;
	wl_display_add_destroy_listener(display, &explicit_sync->display_destroy);

	return explicit_sync;
}

bool wlr_linux_explicit_synchronization_v1_signal_surface_timeline(
		struct wlr_linux_explicit_synchronization_v1 *explicit_sync,
		struct wlr_surface *surface, struct wlr_render_timeline *timeline,
		uint64_t dst_point) {
	struct wlr_linux_surface_synchronization_v1 *surface_sync =
		surface_sync_from_surface(explicit_sync, surface);
	if (!surface_sync) {
		// TODO: fallback to DMA-BUF fence export
		return false;
	}

	return wlr_render_timeline_import_sync_file(timeline, dst_point,
		surface_sync->current.acquire_fence_fd);
}

bool wlr_linux_explicit_synchronization_v1_wait_surface_timeline(
		struct wlr_linux_explicit_synchronization_v1 *explicit_sync,
		struct wlr_surface *surface, struct wlr_render_timeline *timeline,
		uint64_t src_point) {
	struct wlr_linux_surface_synchronization_v1 *surface_sync =
		surface_sync_from_surface(explicit_sync, surface);
	if (!surface_sync) {
		return true;
	}

	struct wlr_linux_buffer_release_v1 *buffer_release =
		surface_sync->current.buffer_release;
	surface_sync->current.buffer_release = NULL;
	if (!buffer_release) {
		return true;
	}

	int fence_fd = wlr_render_timeline_export_sync_file(timeline, src_point);
	if (fence_fd < 0) {
		return false;
	}

	buffer_release_destroy(buffer_release, fence_fd);
	close(fence_fd);
	return true;
}
