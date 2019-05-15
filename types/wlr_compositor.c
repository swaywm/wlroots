#include <assert.h>
#include <stdlib.h>
#include <limits.h>
#include <inttypes.h>
#include <wayland-server.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_surface.h>
#include <wlr/util/log.h>
#include "util/signal.h"

#define COMPOSITOR_VERSION 4

static const struct wl_region_interface region_impl;

pixman_region32_t *wlr_region_from_resource(struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource, &wl_region_interface,
		&region_impl));
	return wl_resource_get_user_data(resource);
}

static struct wlr_buffer *compositor_buffer_create(struct wlr_compositor *comp,
		struct wlr_buffer *old, pixman_region32_t *damage,
		struct wl_resource *res) {
	return comp->buffer_impl->create(comp->buffer_data, old, damage, res);
}

static struct wlr_buffer *compositor_buffer_ref(struct wlr_compositor *comp,
		struct wlr_buffer *buf) {
	return comp->buffer_impl->ref(buf);
}

static void compositor_buffer_unref(struct wlr_compositor *comp,
		struct wlr_buffer *buf) {
	comp->buffer_impl->unref(buf);
}

static void region_destroy(struct wl_client *client, struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static void region_add(struct wl_client *client, struct wl_resource *resource,
		int32_t x, int32_t y, int32_t width, int32_t height) {
	pixman_region32_t *region = wlr_region_from_resource(resource);
	pixman_region32_union_rect(region, region, x, y, width, height);
}

static void region_subtract(struct wl_client *client, struct wl_resource *resource,
		int32_t x, int32_t y, int32_t width, int32_t height) {
	pixman_region32_t *region = wlr_region_from_resource(resource);
	pixman_region32_union_rect(region, region, x, y, width, height);

	pixman_region32_t rect;
	pixman_region32_init_rect(&rect, x, y, width, height);
	pixman_region32_subtract(region, region, &rect);
	pixman_region32_fini(&rect);
}

static const struct wl_region_interface region_impl = {
	.destroy = region_destroy,
	.add = region_add,
	.subtract = region_subtract,
};

static void region_resource_destroy(struct wl_resource *resource) {
	pixman_region32_t *region = wlr_region_from_resource(resource);

	pixman_region32_fini(region);
	free(region);
}

static struct wlr_commit *commit_create(struct wlr_surface_2 *surf) {
	struct wlr_compositor *comp = surf->compositor;

	struct wlr_commit *c = calloc(1, sizeof(*c));
	if (!c) {
		wlr_log_errno(WLR_ERROR, "Allocation failed");
		return NULL;
	}

	// Prevent a zero allocation
	c->state_len = comp->ids ? comp->ids : 1;
	c->state = calloc(c->state_len, sizeof(*c->state));
	if (!c->state) {
		wlr_log_errno(WLR_ERROR, "Allocation failed");
		free(c);
		return NULL;
	}

	c->compositor = comp;
	c->surface = surf;
	wl_signal_init(&c->events.commit);
	wl_signal_init(&c->events.complete);
	wl_signal_init(&c->events.destroy);

	wl_list_init(&c->frame_callbacks);
	pixman_region32_init(&c->opaque_region);
	pixman_region32_init_rect(&c->input_region,
		INT_MIN, INT_MIN, UINT_MAX, UINT_MAX);
	c->scale = 1;
	pixman_region32_init(&c->damage);

	return c;
}

static void commit_destroy(struct wlr_commit *c) {
	pixman_region32_fini(&c->opaque_region);
	pixman_region32_fini(&c->input_region);
	pixman_region32_fini(&c->damage);

	if (c->buffer) {
		compositor_buffer_unref(c->compositor, c->buffer);
	}

	wlr_signal_emit_safe(&c->events.destroy, c);

	free(c->state);
	free(c);
}

bool wlr_commit_is_complete(struct wlr_commit *c) {
	return c->committed && c->inhibit == 0;
}

static bool commit_is_latest(struct wlr_commit *c) {
	assert(c->committed);

	struct wlr_surface_2 *surf = c->surface;
	struct wlr_commit *iter;
	wl_list_for_each(iter, &surf->committed, link) {
		if (iter == c) {
			return true;
		}

		if (wlr_commit_is_complete(iter)) {
			return false;
		}
	}

	// You shouldn't be able to get here.
	abort();
}

static void surface_prune_commits(struct wlr_surface_2 *surf) {
	bool complete = false;
	struct wlr_commit *iter, *tmp;
	wl_list_for_each_safe(iter, tmp, &surf->committed, link) {
		if (!complete) {
			complete = wlr_commit_is_complete(iter);
		} else if (iter->ref_cnt == 0) {
			wl_list_remove(&iter->link);
			commit_destroy(iter);
		}
	}
}

void wlr_commit_inhibit(struct wlr_commit *commit) {
	assert(commit && !wlr_commit_is_complete(commit));
	++commit->inhibit;
}

void wlr_commit_uninhibit(struct wlr_commit *commit) {
	assert(commit && commit->inhibit > 0);
	--commit->inhibit;

	if (wlr_commit_is_complete(commit)) {
		wlr_signal_emit_safe(&commit->events.complete, commit);

		if (commit_is_latest(commit)) {
			struct wlr_surface_2 *surf = commit->surface;
			surface_prune_commits(surf);
			wlr_signal_emit_safe(&surf->events.commit, surf);
		}
	}
}

void wlr_commit_set(struct wlr_commit *commit, uint32_t id, void *data) {
	struct wlr_compositor *comp = commit->surface->compositor;

	// They didn't get their ID from wlr_compositor_register
	assert(id < comp->ids);

	// This can happen if wlr_compositor_register is called after
	// the commit was created.
	if (commit->state_len < comp->ids) {
		void **tmp = realloc(commit->state,
			sizeof(*commit->state) * comp->ids);
		if (!tmp) {
			wlr_log_errno(WLR_ERROR, "Allocation failed");
			return;
		}

		commit->state = tmp;
		for (size_t i = commit->state_len; i < comp->ids; ++i) {
			commit->state[i] = NULL;
		}
		commit->state_len = comp->ids;
	}

	commit->state[id] = data;
}

void *wlr_commit_get(struct wlr_commit *commit, uint32_t id) {
	if (id >= commit->state_len) {
		return NULL;
	}
	return commit->state[id];
};

static void surface_destroy(struct wl_client *client, struct wl_resource *res) {
	wl_resource_destroy(res);
}

static void surface_attach(struct wl_client *client, struct wl_resource *res,
		struct wl_resource *buffer, int32_t dx, int32_t dy) {
	struct wlr_surface_2 *surf = wlr_surface_from_resource_2(res);
	struct wlr_commit *commit = wlr_surface_get_pending(surf);

	commit->buffer_resource = buffer;
	commit->sx = dx;
	commit->sy = dy;
}

static void surface_damage(struct wl_client *client, struct wl_resource *res,
		int32_t x, int32_t y, int32_t width, int32_t height) {
	struct wlr_surface_2 *surf = wlr_surface_from_resource_2(res);
	struct wlr_commit *commit = wlr_surface_get_pending(surf);

	/*
	 * We choose to not track surface-coordinate damage; it's not worth the
	 * effort. Clients are recommended by the wayland protocol to use
	 * buffer-coordinate damage instead.
	 *
	 * We still need to acknowledge that damage happened, so we damage the
	 * entire buffer instead.
	 */

	pixman_region32_union_rect(&commit->damage,
		&commit->damage, 0, 0, UINT_MAX, UINT_MAX);
}

static void callback_resource_destroy(struct wl_resource *resource) {
	wl_list_remove(wl_resource_get_link(resource));
}

static void surface_frame(struct wl_client *client, struct wl_resource *res,
		uint32_t id) {
	struct wlr_surface_2 *surf = wlr_surface_from_resource_2(res);
	struct wlr_commit *commit = wlr_surface_get_pending(surf);

	struct wl_resource *callback =
		wl_resource_create(client, &wl_callback_interface, 1, id);
	if (!callback) {
		wlr_log_errno(WLR_ERROR, "Failed to create callback resource");
		wl_resource_post_no_memory(surf->resource);
		return;
	}

	wl_resource_set_implementation(callback, NULL, NULL,
		callback_resource_destroy);

	wl_list_insert(&commit->frame_callbacks, wl_resource_get_link(callback));
}

static void surface_set_opaque_region(struct wl_client *client,
		struct wl_resource *res, struct wl_resource *region_res) {
	struct wlr_surface_2 *surf = wlr_surface_from_resource_2(res);
	struct wlr_commit *commit = wlr_surface_get_pending(surf);

	if (region_res) {
		pixman_region32_t *region = wlr_region_from_resource(region_res);
		pixman_region32_copy(&commit->opaque_region, region);
	} else {
		pixman_region32_clear(&commit->opaque_region);
	}
}

static void surface_set_input_region(struct wl_client *client,
		struct wl_resource *res, struct wl_resource *input_res) {
	struct wlr_surface_2 *surf = wlr_surface_from_resource_2(res);
	struct wlr_commit *commit = wlr_surface_get_pending(surf);

	if (input_res) {
		pixman_region32_t *region = wlr_region_from_resource(input_res);
		pixman_region32_copy(&commit->input_region, region);
	} else {
		pixman_region32_union_rect(&commit->input_region,
			&commit->input_region, INT_MIN, INT_MIN, UINT_MAX, UINT_MAX);
	}
}

static void surface_set_buffer_transform(struct wl_client *client,
		struct wl_resource *res, int32_t transform) {
	struct wlr_surface_2 *surf = wlr_surface_from_resource_2(res);
	struct wlr_commit *commit = wlr_surface_get_pending(surf);

	if (transform < 0 || transform > WL_OUTPUT_TRANSFORM_FLIPPED_270) {
		wl_resource_post_error(surf->resource,
			WL_SURFACE_ERROR_INVALID_TRANSFORM,
			"transform value (%"PRId32") is not valid wl_output.transform enum",
			transform);
		return;
	}

	commit->transform = transform;
}

static void surface_set_buffer_scale(struct wl_client *client,
		struct wl_resource *res, int32_t scale) {
	struct wlr_surface_2 *surf = wlr_surface_from_resource_2(res);
	struct wlr_commit *commit = wlr_surface_get_pending(surf);

	if (scale <= 0) {
		wl_resource_post_error(surf->resource,
			WL_SURFACE_ERROR_INVALID_SCALE,
			"scale value (%"PRId32") is not positive",
			scale);
		return;
	}

	commit->scale = scale;
}

static void surface_damage_buffer(struct wl_client *client, struct wl_resource *res,
		int32_t x, int32_t y, int32_t width, int32_t height) {
	struct wlr_surface_2 *surf = wlr_surface_from_resource_2(res);
	struct wlr_commit *commit = wlr_surface_get_pending(surf);

	/*
	 * The client is being extremely stupid, but there is nothing in the
	 * standard mentioning that this is an error, so we just ignore it.
	 */
	if (width < 0 || height < 0) {
		return;
	}

	pixman_region32_union_rect(&commit->damage,
		&commit->damage, x, y, width, height);
}

static void surface_commit(struct wl_client *client, struct wl_resource *res) {
	struct wlr_surface_2 *surf = wlr_surface_from_resource_2(res);
	struct wlr_compositor *comp = surf->compositor;
	struct wlr_commit *commit = wlr_surface_get_pending(surf);
	struct wlr_commit *previous = wlr_surface_get_latest(surf);

	commit->committed = true;
	wl_list_insert(&surf->committed, &commit->link);

	if (commit->buffer_resource) {
		/*
		 * If we're complete, that means that we can safely "steal" the
		 * buffer from the previous commit, as we're going to prune it
		 * right after this block.
		 *
		 * This is an optimisation for wl_shm buffers specifically,
		 * and allows us to skip some allocations and copies.
		 */
		struct wlr_buffer *old = NULL;
		if (previous && previous->ref_cnt == 1 && wlr_commit_is_complete(commit)) {
			old = previous->buffer;
			previous->buffer = NULL;
		}

		commit->buffer = compositor_buffer_create(comp, old, &commit->damage,
			commit->buffer_resource);
	} else if (previous && previous->buffer) {
		commit->buffer = compositor_buffer_ref(comp, previous->buffer);
	}

	if (previous) {
		wlr_commit_unref(previous);
	}

	/*
	 * The wayland protocol says we'll signal these in the order they're
	 * committed, so we make sure to add them to the end of the list.
	 */
	wl_list_insert_list(surf->frame_callbacks.prev, &commit->frame_callbacks);
	wl_list_init(&commit->frame_callbacks);

	wlr_signal_emit_safe(&commit->events.commit, commit);

	surf->pending = commit_create(surf);
	if (!surf->pending) {
		wl_resource_post_no_memory(surf->resource);
		return;
	}

	/*
	 * Copy old wl_surface state to next pending commit.
	 */
	pixman_region32_copy(&surf->pending->opaque_region, &commit->opaque_region);
	pixman_region32_copy(&surf->pending->input_region, &commit->input_region);
	surf->pending->transform = commit->transform;
	surf->pending->scale = commit->scale;


	struct wlr_compositor_new_state_args args = {
		.old = commit,
		.new = surf->pending,
	};

	wlr_signal_emit_safe(&surf->compositor->events.new_state, &args);

	surface_prune_commits(surf);

	if (wlr_commit_is_complete(commit)) {
		wlr_signal_emit_safe(&commit->events.complete, commit);
		wlr_signal_emit_safe(&surf->events.commit, surf);
	}
}

static struct wl_surface_interface surface_impl = {
	.destroy = surface_destroy,
	.attach = surface_attach,
	.damage = surface_damage,
	.frame = surface_frame,
	.set_opaque_region = surface_set_opaque_region,
	.set_input_region = surface_set_input_region,
	.set_buffer_transform = surface_set_buffer_transform,
	.set_buffer_scale = surface_set_buffer_scale,
	.damage_buffer = surface_damage_buffer,
	.commit = surface_commit,
};

struct wlr_surface_2 *wlr_surface_from_resource_2(struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource, &wl_surface_interface,
		&surface_impl));
	return wl_resource_get_user_data(resource);
}

static void surface_resource_destroy(struct wl_resource *resource) {
	struct wlr_surface_2 *surf = wlr_surface_from_resource_2(resource);

	wlr_signal_emit_safe(&surf->events.destroy, surf);

	struct wlr_commit *iter, *tmp;
	wl_list_for_each_safe(iter, tmp, &surf->committed, link) {
		if (iter->ref_cnt == 0) {
			commit_destroy(iter);
		}
	}

	commit_destroy(surf->pending);

	free(surf);
}

static const struct wl_compositor_interface compositor_impl;
static struct wlr_compositor *compositor_from_resource(struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource, &wl_compositor_interface,
		&compositor_impl));
	return wl_resource_get_user_data(resource);
}

static void compositor_create_surface(struct wl_client *client,
		struct wl_resource *resource, uint32_t id) {
	struct wlr_compositor *compositor = compositor_from_resource(resource);

	struct wlr_surface_2 *surf = calloc(1, sizeof(*surf));
	if (!surf) {
		wlr_log_errno(WLR_ERROR, "Allocation failed");
		goto error_post;
	}

	surf->resource = wl_resource_create(client, &wl_surface_interface,
		wl_resource_get_version(resource), id);
	if (!surf->resource) {
		wlr_log_errno(WLR_ERROR, "Failed to create surface resource");
		goto error_surf;
	}

	wl_resource_set_implementation(surf->resource, &surface_impl,
		surf, surface_resource_destroy);

	surf->compositor = compositor;
	wl_list_init(&surf->committed);
	wl_list_init(&surf->frame_callbacks);
	wl_signal_init(&surf->events.commit);
	wl_signal_init(&surf->events.destroy);

	surf->pending = commit_create(surf);
	if (!surf->pending) {
		goto error_resource;
	}

	struct wlr_compositor_new_state_args args = {
		.old = NULL,
		.new = surf->pending,
	};

	wlr_signal_emit_safe(&compositor->events.new_surface_2, surf);
	wlr_signal_emit_safe(&compositor->events.new_state, &args);

	return;

error_resource:
	wl_resource_destroy(surf->resource);
error_surf:
	free(surf);
error_post:
	wl_client_post_no_memory(client);
}

static void compositor_create_region(struct wl_client *client,
		struct wl_resource *resource, uint32_t id) {
	pixman_region32_t *region = calloc(1, sizeof(*region));
	if (!region) {
		wlr_log_errno(WLR_ERROR, "Allocation failed");
		wl_client_post_no_memory(client);
		return;
	}

	pixman_region32_init(region);

	struct wl_resource *res =
		wl_resource_create(client, &wl_region_interface, 1, id);
	if (!res) {
		free(region);
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(res, &region_impl, region,
		region_resource_destroy);
}

static const struct wl_compositor_interface compositor_impl = {
	.create_surface = compositor_create_surface,
	.create_region = compositor_create_region,
};

static void compositor_bind(struct wl_client *client, void *data,
		uint32_t version, uint32_t id) {
	struct wlr_compositor *comp = data;

	struct wl_resource *res =
		wl_resource_create(client, &wl_compositor_interface, version, id);
	if (!res) {
		wlr_log_errno(WLR_ERROR, "Failed to create compositor resource");
		wl_client_post_no_memory(client);
		return;
	}

	wl_resource_set_implementation(res, &compositor_impl, comp, NULL);
}

static void compositor_display_destroy(struct wl_listener *listener, void *data) {
	struct wlr_compositor *comp = wl_container_of(listener, comp, display_destroy);

	wl_global_destroy(comp->global);
	free(comp);
}

struct wlr_compositor *wlr_compositor_create(struct wl_display *display) {
	struct wlr_compositor *comp = calloc(1, sizeof(*comp));
	if (!comp) {
		wlr_log_errno(WLR_ERROR, "Allocation failed");
		return NULL;
	}

	comp->display = display;
	comp->global = wl_global_create(display, &wl_compositor_interface,
		COMPOSITOR_VERSION, comp, compositor_bind);
	if (!comp->global) {
		wlr_log_errno(WLR_ERROR, "Failed to create wayland global");
		free(comp);
		return NULL;
	}

	wl_signal_init(&comp->events.new_surface);
	wl_signal_init(&comp->events.new_surface_2);
	wl_signal_init(&comp->events.new_state);

	comp->display_destroy.notify = compositor_display_destroy;
	wl_display_add_destroy_listener(display, &comp->display_destroy);

	return comp;
}

void wlr_compositor_set_buffer_impl(struct wlr_compositor *comp,
		void *data, const struct wlr_compositor_buffer_impl *impl) {
	assert(comp);
	assert(impl);
	assert(!comp->buffer_impl);

	comp->buffer_data = data;
	comp->buffer_impl = impl;
}

uint32_t wlr_compositor_register(struct wlr_compositor *compositor) {
	assert(compositor);
	return compositor->ids++;
}

struct wlr_commit *wlr_surface_get_commit(struct wlr_surface_2 *surf) {
	surface_prune_commits(surf);

	struct wlr_commit *iter;
	wl_list_for_each(iter, &surf->committed, link) {
		if (wlr_commit_is_complete(iter)) {
			++iter->ref_cnt;
			return iter;
		}
	}

	return NULL;
}

struct wlr_commit *wlr_surface_get_pending(struct wlr_surface_2 *surf) {
	return surf->pending;
}

struct wlr_commit *wlr_surface_get_latest(struct wlr_surface_2 *surf) {
	if (wl_list_empty(&surf->committed)) {
		return NULL;
	}

	struct wlr_commit *first = wl_container_of(surf->committed.next, first, link);
	++first->ref_cnt;
	return first;
}

void wlr_surface_send_enter_2(struct wlr_surface_2 *surf, struct wlr_output *output) {
	struct wl_client *client = wl_resource_get_client(surf->resource);
	struct wl_resource *iter;
	wl_resource_for_each(iter, &output->resources) {
		if (wl_resource_get_client(iter) == client) {
			wl_surface_send_enter(surf->resource, iter);
		}
	}
}

void wlr_surface_send_leave_2(struct wlr_surface_2 *surf, struct wlr_output *output) {
	struct wl_client *client = wl_resource_get_client(surf->resource);
	struct wl_resource *iter;
	wl_resource_for_each(iter, &output->resources) {
		if (wl_resource_get_client(iter) == client) {
			wl_surface_send_leave(surf->resource, iter);
		}
	}
}

static inline int64_t timespec_to_msec(const struct timespec *a) {
	return (int64_t)a->tv_sec * 1000 + a->tv_nsec / 1000000;
}

void wlr_surface_send_frame_done_2(struct wlr_surface_2 *surf,
		const struct timespec *ts) {
	struct wl_resource *iter, *tmp;
	wl_resource_for_each_safe(iter, tmp, &surf->frame_callbacks) {
		wl_callback_send_done(iter, timespec_to_msec(ts));
		wl_resource_destroy(iter);
	}
}

struct wlr_commit *wlr_commit_ref(struct wlr_commit *commit) {
	assert(commit);
	++commit->ref_cnt;

	return commit;
}

void wlr_commit_unref(struct wlr_commit *commit) {
	assert(commit && commit->ref_cnt > 0);
	--commit->ref_cnt;

	surface_prune_commits(commit->surface);
}

bool wlr_surface_set_role_2(struct wlr_surface_2 *surf, const char *name) {
	assert(surf && name);

	/* Already has role */
	if (surf->role_name && strcmp(surf->role_name, name) != 0) {
		return false;
	}

	surf->role_name = name;
	return true;
}
