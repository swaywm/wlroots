#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <wlr/types/wlr_surface.h>
#include <wlr/types/wlr_screenlocker_v1.h>
#include "util/signal.h"
#include "wp-screenlocker-unstable-v1-protocol.h"

static void resource_destroy(struct wl_client *client, struct wl_resource *resource)
{
        wl_resource_destroy(resource);
}

struct wlr_locker_state {
	struct wl_list link; // sway_lock_state::locker_globals
	struct wl_resource *resource; // zwp_screenlocker_v1
	struct wlr_screenlock_manager_v1 *manager;
};

static void locker_resource_destroy(struct wl_resource *resource) {
	struct wlr_locker_state* state = wl_resource_get_user_data(resource);
	wl_list_remove(&state->link);
	free(state);
}

static void handle_lock_unlock(struct wl_client *client,
		struct wl_resource *resource) {
	struct wlr_screenlock_manager_v1 *manager = wl_resource_get_user_data(resource);

	if (!manager) {
		return;
	}
	// after an unlock, this handle becomes inert
	wl_resource_set_user_data(resource, NULL);
	manager->lock_resource = NULL;

	struct wlr_screenlock_change signal = {
		.old_client = client,
		.new_client = NULL,
		.how = WLR_SCREENLOCK_MODE_CHANGE_UNLOCK,
	};

	manager->locked = false;
	wlr_signal_emit_safe(&manager->events.change_request, &signal);
}

void wlr_screenlock_send_unlock_finished(struct wlr_screenlock_manager_v1 *manager) {
	struct wlr_locker_state *locker;
	wl_list_for_each(locker, &manager->locker_globals, link) {
		zwp_screenlocker_v1_send_unlocked(locker->resource);
	}
}

static const struct zwp_screenlocker_lock_v1_interface lock_impl = {
	.unlock = handle_lock_unlock,
	.destroy = resource_destroy,
};

static void lock_resource_destroy(struct wl_resource *resource) {
	struct wlr_screenlock_manager_v1 *manager = wl_resource_get_user_data(resource);
	// ignore inert objects
	if (!manager)
		return;

	struct wlr_screenlock_change signal = {
		.old_client = wl_resource_get_client(resource),
		.new_client = NULL,
		.how = WLR_SCREENLOCK_MODE_CHANGE_ABANDON,
	};
	manager->lock_resource = NULL;

	wlr_signal_emit_safe(&manager->events.change_request, &signal);
}

static void handle_locker_lock(struct wl_client *client, struct wl_resource *locker_resource, uint32_t id)
{
	struct wlr_locker_state *locker_state = wl_resource_get_user_data(locker_resource);
	struct wlr_screenlock_manager_v1 *manager = locker_state->manager;
	struct wl_resource *lock_resource = wl_resource_create(client,
		&zwp_screenlocker_lock_v1_interface, 1, id);
	if (lock_resource == NULL) {
		wl_client_post_no_memory(client);
		return;
	}

	struct wlr_screenlock_change signal = {
		.old_client = NULL,
		.new_client = client,
		.how = WLR_SCREENLOCK_MODE_CHANGE_LOCK,
		.reject = false,
	};

	if (manager->lock_resource) {
		signal.how = WLR_SCREENLOCK_MODE_CHANGE_REPLACE;
		signal.old_client = wl_resource_get_client(manager->lock_resource);
	} else if (manager->locked) {
		signal.how = WLR_SCREENLOCK_MODE_CHANGE_RESPAWN;
	}
	wlr_signal_emit_safe(&manager->events.change_request, &signal);

	wl_resource_set_implementation(lock_resource,
		&lock_impl, signal.reject ? NULL : manager,
		lock_resource_destroy);

	if (signal.reject) {
		if (manager->lock_resource) {
			zwp_screenlocker_lock_v1_send_rejected(lock_resource, "locker already present");
		} else {
			zwp_screenlocker_lock_v1_send_rejected(lock_resource, NULL);
		}
		return;
	}

	if (manager->lock_resource) {
		wl_resource_set_user_data(manager->lock_resource, NULL);
		zwp_screenlocker_lock_v1_send_rejected(manager->lock_resource, "replaced");
	}

	manager->lock_resource = lock_resource;

	if (!manager->locked) {
		manager->locked = true;
		// only broadcast when locking the first time
		struct wlr_locker_state *locker;
		wl_list_for_each(locker, &manager->locker_globals, link) {
			zwp_screenlocker_v1_send_locked(locker->resource);
		}
	}
}

void wlr_screenlock_send_lock_finished(struct wlr_screenlock_manager_v1 *manager) {
	if (manager->lock_resource)
		zwp_screenlocker_lock_v1_send_locked(manager->lock_resource);
}

static void handle_set_visibility(struct wl_client *client, struct wl_resource *resource, uint32_t visibility)
{
	struct wlr_screenlock_lock_surface* state = wl_resource_get_user_data(resource);
	if (!state)
		return;
	state->pending_mode = visibility;
}

static const struct zwp_screenlocker_visibility_v1_interface visibility_impl = {
	.destroy = resource_destroy,
	.set_visibility = handle_set_visibility,
};

static void vis_surface_commit(struct wl_listener *listener, void *data) {
	struct wlr_screenlock_lock_surface *state = wl_container_of(listener, state, surface_commit);
	if (state->pending_mode == state->current_mode)
		return;
	state->current_mode = state->pending_mode;
}

static void vis_surface_destroy(struct wl_listener *listener, void *data) {
	struct wlr_screenlock_lock_surface *state = wl_container_of(listener, state, surface_destroy);
	wl_list_remove(&state->surface_destroy.link);
	wl_list_remove(&state->link);
	wl_resource_set_user_data(state->resource, NULL);
	free(state);
}

static void vis_resource_destroy(struct wl_resource *resource) {
	struct wlr_screenlock_lock_surface* state = wl_resource_get_user_data(resource);
	if (!state)
		return;
	wl_list_remove(&state->surface_destroy.link);
	wl_list_remove(&state->link);
	wl_resource_set_user_data(state->resource, NULL);
	free(state);
}

static void handle_get_visibility(struct wl_client *client, struct wl_resource *locker_resource, uint32_t id, struct wl_resource *surface_resource)
{
	struct wlr_locker_state *locker_state = wl_resource_get_user_data(locker_resource);
	struct wlr_surface *surface = wlr_surface_from_resource(surface_resource);
	struct wlr_screenlock_manager_v1 *manager = locker_state->manager;

	struct wlr_screenlock_lock_surface *state = calloc(1, sizeof(*state));
	if (state == NULL) {
		wl_client_post_no_memory(client);
		return;
	}

	state->resource = wl_resource_create(client,
		&zwp_screenlocker_visibility_v1_interface, 1, id);
	if (state->resource == NULL) {
		free(state);
		wl_client_post_no_memory(client);
		return;
	}

	state->surface = surface;
	state->manager = manager;
	wl_signal_add(&surface->events.commit, &state->surface_commit);
	state->surface_commit.notify = vis_surface_commit;
	wl_signal_add(&surface->events.destroy, &state->surface_destroy);
	state->surface_destroy.notify = vis_surface_destroy;
	wl_list_insert(&manager->lock_surfaces, &state->link);

	// TODO reject non-layer-shell surfaces?

	wl_resource_set_implementation(state->resource,
		&visibility_impl, state,
		vis_resource_destroy);
}

static const struct zwp_screenlocker_v1_interface unlock_impl = {
	.destroy = resource_destroy,
	.get_visibility = handle_get_visibility,
	.lock = handle_locker_lock,
};

static void screenlock_bind(struct wl_client *client, void *data,
		uint32_t version, uint32_t id) {
	struct wlr_screenlock_manager_v1 *manager = data;

	struct wlr_locker_state *state = calloc(1, sizeof(*state));
	if (!state) {
		wl_client_post_no_memory(client);
		return;
	}

	state->manager = manager;
	state->resource = wl_resource_create(client,
		&zwp_screenlocker_v1_interface, version, id);

	if (!state->resource) {
		free(state);
		wl_client_post_no_memory(client);
		return;
	}

	wl_resource_set_implementation(state->resource, &unlock_impl, state, locker_resource_destroy);
	wl_list_insert(&manager->locker_globals, &state->link);

	if (manager->locked) {
		zwp_screenlocker_v1_send_locked(state->resource);
	} else {
		zwp_screenlocker_v1_send_unlocked(state->resource);
	}
}

struct wlr_screenlock_manager_v1 *wlr_screenlock_manager_v1_create(
		struct wl_display *display) {
	struct wlr_screenlock_manager_v1 *manager = calloc(1, sizeof(*manager));
	if (!manager) {
		return NULL;
	}

	manager->global = wl_global_create(display, &zwp_screenlocker_v1_interface,
			1, manager, screenlock_bind);

	if (!manager->global) {
		free(manager);
		return NULL;
	}

	wl_list_init(&manager->locker_globals);
	wl_list_init(&manager->lock_surfaces);
	wl_signal_init(&manager->events.change_request);

	return manager;
}
