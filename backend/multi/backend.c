#include <stdbool.h>
#include <stdlib.h>
#include <wlr/backend/interface.h>
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

static bool multi_backend_init(struct wlr_backend_state *state) {
	for (size_t i = 0; i < state->backends->length; ++i) {
		struct subbackend_state *sub = state->backends->items[i];
		if (!wlr_backend_init(sub->backend)) {
			wlr_log(L_ERROR, "Failed to initialize backend %zd", i);
			return false;
		}
	}
	return true;
}

static void multi_backend_destroy(struct wlr_backend_state *state) {
	for (size_t i = 0; i < state->backends->length; ++i) {
		struct subbackend_state *sub = state->backends->items[i];
		wlr_backend_destroy(sub->backend);
		free(sub);
	}
	list_free(state->backends);
	free(state);
}

struct wlr_backend_impl backend_impl = {
	.init = multi_backend_init,
	.destroy = multi_backend_destroy
};

struct wlr_backend *wlr_multi_backend_create() {
	struct wlr_backend_state *state =
		calloc(1, sizeof(struct wlr_backend_state));
	if (!state) {
		wlr_log(L_ERROR, "Backend allocation failed");
		return NULL;
	}
	state->backends = list_create();
	if (!state->backends) {
		free(state);
		wlr_log(L_ERROR, "Backend allocation failed");
		return NULL;
	}
	struct wlr_backend *backend = wlr_backend_create(&backend_impl, state);
	state->backend = backend;
	return backend;
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

void wlr_multi_backend_add(struct wlr_backend *multi,
		struct wlr_backend *backend) {
	struct subbackend_state *sub = calloc(1, sizeof(struct subbackend_state));
	sub->backend = backend;
	sub->container = multi;

	sub->input_add.notify = input_add_reemit;
	sub->input_remove.notify = input_remove_reemit;
	sub->output_add.notify = output_add_reemit;
	sub->output_remove.notify = output_remove_reemit;

	wl_list_init(&sub->input_add.link);
	wl_list_init(&sub->input_remove.link);
	wl_list_init(&sub->output_add.link);
	wl_list_init(&sub->output_remove.link);

	wl_signal_add(&backend->events.input_add, &sub->input_add);
	wl_signal_add(&backend->events.input_remove, &sub->input_remove);
	wl_signal_add(&backend->events.output_add, &sub->output_add);
	wl_signal_add(&backend->events.output_remove, &sub->output_remove);

	list_add(multi->state->backends, sub);
}
