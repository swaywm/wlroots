#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <wlr/backend/interface.h>
#include <wlr/backend/session.h>
#include <wlr/util/log.h>
#include "backend/multi.h"

struct subbackend_state {
	struct wlr_backend *backend;
	struct wlr_backend *container;
	struct wl_listener input_add;
	struct wl_listener input_remove;
	struct wl_listener output_add;
	struct wl_listener output_remove;
};

static bool multi_backend_start(struct wlr_backend *_backend) {
	struct wlr_multi_backend *backend = (struct wlr_multi_backend *)_backend;
	for (size_t i = 0; i < backend->backends->length; ++i) {
		struct subbackend_state *sub = backend->backends->items[i];
		if (!wlr_backend_start(sub->backend)) {
			wlr_log(L_ERROR, "Failed to initialize backend %zd", i);
			return false;
		}
	}
	return true;
}

static void multi_backend_destroy(struct wlr_backend *_backend) {
	struct wlr_multi_backend *backend = (struct wlr_multi_backend *)_backend;
	for (size_t i = 0; i < backend->backends->length; ++i) {
		struct subbackend_state *sub = backend->backends->items[i];
		wlr_backend_destroy(sub->backend);
		free(sub);
	}
	list_free(backend->backends);
	wlr_session_destroy(backend->session);
	free(backend);
}

static struct wlr_egl *multi_backend_get_egl(struct wlr_backend *_backend) {
	struct wlr_multi_backend *backend = (struct wlr_multi_backend *)_backend;
	for (size_t i = 0; i < backend->backends->length; ++i) {
		struct subbackend_state *sub = backend->backends->items[i];
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
	.get_egl = multi_backend_get_egl
};

struct wlr_backend *wlr_multi_backend_create(struct wlr_session *session) {
	struct wlr_multi_backend *backend =
		calloc(1, sizeof(struct wlr_multi_backend));
	if (!backend) {
		wlr_log(L_ERROR, "Backend allocation failed");
		return NULL;
	}

	backend->backends = list_create();
	if (!backend->backends) {
		free(backend);
		wlr_log(L_ERROR, "Backend allocation failed");
		return NULL;
	}

	wlr_backend_init(&backend->backend, &backend_impl);

	backend->session = session;
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

void wlr_multi_backend_add(struct wlr_backend *_multi,
		struct wlr_backend *backend) {
	assert(wlr_backend_is_multi(_multi));

	struct wlr_multi_backend *multi = (struct wlr_multi_backend *)_multi;
	struct subbackend_state *sub;
	if (!(sub = calloc(1, sizeof(struct subbackend_state)))) {
		wlr_log(L_ERROR, "Could not add backend: allocation failed");
		return;
	}
	if (list_add(multi->backends, sub) == -1) {
		wlr_log(L_ERROR, "Could not add backend: allocation failed");
		free(sub);
		return;
	}

	sub->backend = backend;
	sub->container = &multi->backend;

	sub->input_add.notify = input_add_reemit;
	sub->input_remove.notify = input_remove_reemit;
	sub->output_add.notify = output_add_reemit;
	sub->output_remove.notify = output_remove_reemit;

	wl_signal_add(&backend->events.input_add, &sub->input_add);
	wl_signal_add(&backend->events.input_remove, &sub->input_remove);
	wl_signal_add(&backend->events.output_add, &sub->output_add);
	wl_signal_add(&backend->events.output_remove, &sub->output_remove);
}

struct wlr_session *wlr_multi_get_session(struct wlr_backend *_backend) {
	assert(wlr_backend_is_multi(_backend));

	struct wlr_multi_backend *backend = (struct wlr_multi_backend *)_backend;
	return backend->session;
}
