#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <wlr/backend/interface.h>
#include <wlr/backend/session.h>
#include <wlr/util/log.h>
#include "backend/multi.h"
#include "backend/drm/drm.h"

struct subbackend_state {
	struct wlr_backend *backend;
	struct wlr_backend *container;
	struct wl_listener input_add;
	struct wl_listener input_remove;
	struct wl_listener output_add;
	struct wl_listener output_remove;
	struct wl_listener backend_destroy;
	struct wl_list link;
};

static bool multi_backend_start(struct wlr_backend *wlr_backend) {
	struct wlr_multi_backend *backend = (struct wlr_multi_backend *)wlr_backend;
	struct subbackend_state *sub;
	wl_list_for_each(sub, &backend->backends, link) {
		if (!wlr_backend_start(sub->backend)) {
			wlr_log(L_ERROR, "Failed to initialize backend.");
			return false;
		}
	}
	return true;
}

static void subbackend_state_destroy(struct subbackend_state *sub) {
	wl_list_remove(&sub->input_add.link);
	wl_list_remove(&sub->input_remove.link);
	wl_list_remove(&sub->output_add.link);
	wl_list_remove(&sub->output_remove.link);
	wl_list_remove(&sub->backend_destroy.link);
	wl_list_remove(&sub->link);
	free(sub);
}

static void multi_backend_destroy(struct wlr_backend *wlr_backend) {
	struct wlr_multi_backend *backend = (struct wlr_multi_backend *)wlr_backend;
	struct subbackend_state *sub, *next;
	wl_list_for_each_safe(sub, next, &backend->backends, link) {
		// XXX do we really want to take ownership over added backends?
		wlr_backend_destroy(sub->backend);
	}
	free(backend);
}

static struct wlr_egl *multi_backend_get_egl(struct wlr_backend *wlr_backend) {
	struct wlr_multi_backend *backend = (struct wlr_multi_backend *)wlr_backend;
	struct subbackend_state *sub;
	wl_list_for_each(sub, &backend->backends, link) {
		struct wlr_egl *egl = wlr_backend_get_egl(sub->backend);
		if (egl) {
			return egl;
		}
	}
	return NULL;
}

struct wlr_backend_impl backend_impl = {
	.start = multi_backend_start,
	.destroy = multi_backend_destroy,
	.get_egl = multi_backend_get_egl,
};

struct wlr_backend *wlr_multi_backend_create(struct wl_display *display) {
	struct wlr_multi_backend *backend =
		calloc(1, sizeof(struct wlr_multi_backend));
	if (!backend) {
		wlr_log(L_ERROR, "Backend allocation failed");
		return NULL;
	}

	wl_list_init(&backend->backends);
	wlr_backend_init(&backend->backend, &backend_impl);

	wl_signal_init(&backend->events.backend_add);
	wl_signal_init(&backend->events.backend_remove);

	return &backend->backend;
}

bool wlr_backend_is_multi(struct wlr_backend *b) {
	return b->impl == &backend_impl;
}

static void input_add_reemit(struct wl_listener *listener, void *data) {
	struct subbackend_state *state = wl_container_of(listener,
			state, input_add);
	wl_signal_emit(&state->container->events.input_add, data);
}

static void input_remove_reemit(struct wl_listener *listener, void *data) {
	struct subbackend_state *state = wl_container_of(listener,
			state, input_remove);
	wl_signal_emit(&state->container->events.input_remove, data);
}

static void output_add_reemit(struct wl_listener *listener, void *data) {
	struct subbackend_state *state = wl_container_of(listener,
			state, output_add);
	wl_signal_emit(&state->container->events.output_add, data);
}

static void output_remove_reemit(struct wl_listener *listener, void *data) {
	struct subbackend_state *state = wl_container_of(listener,
			state, output_remove);
	wl_signal_emit(&state->container->events.output_remove, data);
}

static void handle_subbackend_destroy(struct wl_listener *listener,
		void *data) {
	struct subbackend_state *state = wl_container_of(listener,
			state, backend_destroy);
	subbackend_state_destroy(state);
}

static struct subbackend_state *multi_backend_get_subbackend(struct wlr_multi_backend *multi,
		struct wlr_backend *backend) {
	struct subbackend_state *sub = NULL;
	wl_list_for_each(sub, &multi->backends, link) {
		if (sub->backend == backend) {
			return sub;
		}
	}
	return NULL;
}

void wlr_multi_backend_add(struct wlr_backend *_multi,
		struct wlr_backend *backend) {
	assert(wlr_backend_is_multi(_multi));
	struct wlr_multi_backend *multi = (struct wlr_multi_backend *)_multi;

	if (multi_backend_get_subbackend(multi, backend)) {
		// already added
		return;
	}

	struct subbackend_state *sub;
	if (!(sub = calloc(1, sizeof(struct subbackend_state)))) {
		wlr_log(L_ERROR, "Could not add backend: allocation failed");
		return;
	}
	wl_list_insert(&multi->backends, &sub->link);

	sub->backend = backend;
	sub->container = &multi->backend;

	wl_signal_add(&backend->events.destroy, &sub->backend_destroy);
	sub->backend_destroy.notify = handle_subbackend_destroy;

	wl_signal_add(&backend->events.input_add, &sub->input_add);
	sub->input_add.notify = input_add_reemit;

	wl_signal_add(&backend->events.input_remove, &sub->input_remove);
	sub->input_remove.notify = input_remove_reemit;

	wl_signal_add(&backend->events.output_add, &sub->output_add);
	sub->output_add.notify = output_add_reemit;

	wl_signal_add(&backend->events.output_remove, &sub->output_remove);
	sub->output_remove.notify = output_remove_reemit;

	wl_signal_emit(&multi->events.backend_add, backend);
}

void wlr_multi_backend_remove(struct wlr_backend *_multi,
		struct wlr_backend *backend) {
	assert(wlr_backend_is_multi(_multi));
	struct wlr_multi_backend *multi = (struct wlr_multi_backend *)_multi;

	struct subbackend_state *sub =
		multi_backend_get_subbackend(multi, backend);

	if (sub) {
		wl_signal_emit(&multi->events.backend_remove, backend);
		subbackend_state_destroy(sub);
	}
}

struct wlr_session *wlr_multi_get_session(struct wlr_backend *_backend) {
	assert(wlr_backend_is_multi(_backend));

	struct wlr_multi_backend *backend = (struct wlr_multi_backend *)_backend;
	struct subbackend_state *sub;
	wl_list_for_each(sub, &backend->backends, link) {
		if (wlr_backend_is_drm(sub->backend)) {
			return wlr_drm_backend_get_session(sub->backend);
		}
	}
	return NULL;
}

bool wlr_multi_is_empty(struct wlr_backend *_backend) {
	assert(wlr_backend_is_multi(_backend));
	struct wlr_multi_backend *backend = (struct wlr_multi_backend *)_backend;
	return wl_list_length(&backend->backends) < 1;
}
