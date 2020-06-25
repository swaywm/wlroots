#include <assert.h>
#include <stdlib.h>
#include <unistd.h>
#include <wlr/types/wlr_linux_explicit_synchronization_v1.h>
#include <wlr/types/wlr_surface.h>
#include "linux-explicit-synchronization-unstable-v1-protocol.h"
#include "render/sync_file.h"
#include "util/signal.h"

#define LINUX_EXPLICIT_SYNCHRONIZATION_V1_VERSION 2

static const struct zwp_linux_surface_synchronization_v1_interface
	surface_sync_impl;

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

static void buffer_release_handle_buffer_destroy(struct wl_listener *listener,
		void *data) {
	// This should never happen, a release event should always precede the
	// destroy event
	abort();
}

static void buffer_release_handle_buffer_release(struct wl_listener *listener,
		void *data) {
	struct wlr_linux_buffer_release_v1 *buffer_release =
		wl_container_of(listener, buffer_release, buffer_release);
	if (buffer_release->buffer->out_fence_fd >= 0) {
		zwp_linux_buffer_release_v1_send_fenced_release(
			buffer_release->resource, buffer_release->buffer->out_fence_fd);
	} else {
		zwp_linux_buffer_release_v1_send_immediate_release(
			buffer_release->resource);
	}
	wl_resource_destroy(buffer_release->resource);
}

static void buffer_release_handle_resource_destroy(struct wl_resource *resource) {
	struct wlr_linux_buffer_release_v1 *buffer_release =
		buffer_release_from_resource(resource);
	wl_list_remove(&buffer_release->buffer_destroy.link);
	wl_list_remove(&buffer_release->buffer_release.link);
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

	if (surface_sync->pending_fence_fd >= 0) {
		close(fence_fd);
		wl_resource_post_error(resource,
			ZWP_LINUX_SURFACE_SYNCHRONIZATION_V1_ERROR_DUPLICATE_FENCE,
			"a fence FD was already set for this commit");
		return;
	}

	if (!fd_is_sync_file(fence_fd)) {
		close(fence_fd);
		wl_resource_post_error(resource,
			ZWP_LINUX_SURFACE_SYNCHRONIZATION_V1_ERROR_INVALID_FENCE,
			"the provided FD is not a Linux sync file");
		return;
	}

	surface_sync->pending_fence_fd = fence_fd;
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

	if (surface_sync->pending_buffer_release != NULL) {
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

	wl_list_init(&buffer_release->buffer_destroy.link);
	wl_list_init(&buffer_release->buffer_release.link);

	surface_sync->pending_buffer_release = buffer_release;
}

static const struct zwp_linux_surface_synchronization_v1_interface
		surface_sync_impl = {
	.destroy = surface_sync_handle_destroy,
	.set_acquire_fence = surface_sync_handle_set_acquire_fence,
	.get_release = surface_sync_handle_get_release,
};

static void surface_sync_destroy(
		struct wlr_linux_surface_synchronization_v1 *surface_sync) {
	if (surface_sync == NULL) {
		return;
	}
	wl_list_remove(&surface_sync->surface_destroy.link);
	wl_list_remove(&surface_sync->surface_commit.link);
	wl_resource_set_user_data(surface_sync->resource, NULL);
	if (surface_sync->pending_fence_fd >= 0) {
		close(surface_sync->pending_fence_fd);
	}
	if (surface_sync->pending_buffer_release != NULL) {
		wl_resource_destroy(surface_sync->pending_buffer_release->resource);
	}
	free(surface_sync);
}

static void surface_sync_handle_surface_destroy(struct wl_listener *listener,
		void *data) {
	struct wlr_linux_surface_synchronization_v1 *surface_sync =
		wl_container_of(listener, surface_sync, surface_destroy);
	surface_sync_destroy(surface_sync);
}

static void surface_sync_handle_surface_commit(struct wl_listener *listener,
		void *data) {
	struct wlr_linux_surface_synchronization_v1 *surface_sync =
		wl_container_of(listener, surface_sync, surface_commit);

	if (surface_sync->pending_fence_fd >= 0) {
		if (surface_sync->surface->buffer == NULL) {
			wl_resource_post_error(surface_sync->resource,
				ZWP_LINUX_SURFACE_SYNCHRONIZATION_V1_ERROR_NO_BUFFER,
				"acquire fence FD set but no buffer attached");
			return;
		}

		wlr_buffer_set_in_fence(&surface_sync->surface->buffer->base,
			surface_sync->pending_fence_fd);
	}

	if (surface_sync->pending_buffer_release != NULL) {
		if (surface_sync->surface->buffer == NULL) {
			wl_resource_post_error(surface_sync->resource,
				ZWP_LINUX_SURFACE_SYNCHRONIZATION_V1_ERROR_NO_BUFFER,
				"buffer release requested but no buffer attached");
			return;
		}

		struct wlr_buffer *buffer = &surface_sync->surface->buffer->base;
		struct wlr_linux_buffer_release_v1 *buffer_release =
			surface_sync->pending_buffer_release;

		buffer_release->buffer = buffer;

		buffer_release->buffer_destroy.notify =
			buffer_release_handle_buffer_destroy;
		wl_signal_add(&buffer->events.destroy, &buffer_release->buffer_destroy);

		buffer_release->buffer_release.notify =
			buffer_release_handle_buffer_release;
		wl_signal_add(&buffer->events.release, &buffer_release->buffer_release);
	}

	surface_sync->pending_fence_fd = -1;
	surface_sync->pending_buffer_release = NULL;
}

static void surface_sync_handle_resource_destroy(struct wl_resource *resource) {
	struct wlr_linux_surface_synchronization_v1 *surface_sync =
		surface_sync_from_resource(resource);
	surface_sync_destroy(surface_sync);
}

static void explicit_sync_handle_destroy(struct wl_client *client,
		struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static void explicit_sync_handle_get_synchronization(struct wl_client *client,
		struct wl_resource *resource, uint32_t id,
		struct wl_resource *surface_resource) {
	struct wlr_surface *surface = wlr_surface_from_resource(surface_resource);

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
	surface_sync->pending_fence_fd = -1;

	surface_sync->surface_destroy.notify = surface_sync_handle_surface_destroy;
	wl_signal_add(&surface->events.destroy, &surface_sync->surface_destroy);

	surface_sync->surface_commit.notify = surface_sync_handle_surface_commit;
	wl_signal_add(&surface->events.commit, &surface_sync->surface_commit);
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
