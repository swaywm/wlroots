#define _POSIX_C_SOURCE 200112L
#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <time.h>
#include <wlr/backend/interface.h>
#include <wlr/backend/session.h>
#include <wlr/util/log.h>
#include <include/backend/drm/drm.h>
#include <stdio.h>
#include "backend/multi.h"
#include "util/signal.h"

struct subbackend_state {
	struct wlr_backend *backend;
	struct wlr_backend *container;
	struct wl_listener new_input;
	struct wl_listener new_output;
	struct wl_listener destroy;
	struct wl_list link;
};

static struct wlr_multi_backend *multi_backend_from_backend(
		struct wlr_backend *wlr_backend) {
	assert(wlr_backend_is_multi(wlr_backend));
	return (struct wlr_multi_backend *)wlr_backend;
}

static bool multi_backend_start(struct wlr_backend *wlr_backend) {
	struct wlr_multi_backend *backend = multi_backend_from_backend(wlr_backend);
	struct subbackend_state *sub;
	wl_list_for_each(sub, &backend->backends, link) {
		if (!wlr_backend_start(sub->backend)) {
			wlr_log(WLR_ERROR, "Failed to initialize backend.");
			return false;
		}
	}
	return true;
}

static void subbackend_state_destroy(struct subbackend_state *sub) {
	wl_list_remove(&sub->new_input.link);
	wl_list_remove(&sub->new_output.link);
	wl_list_remove(&sub->destroy.link);
	wl_list_remove(&sub->link);
	free(sub);
}

static void multi_backend_destroy(struct wlr_backend *wlr_backend) {
	struct wlr_multi_backend *backend = multi_backend_from_backend(wlr_backend);

	wl_list_remove(&backend->display_destroy.link);

	if(backend->hotplug.enabled) {
		wl_list_remove(&backend->hotplug.add_gpu_signal.link);
		wl_list_remove(&backend->hotplug.remove_gpu_signal.link);
	}

	// Some backends may depend on other backends, ie. destroying a backend may
	// also destroy other backends
	while (!wl_list_empty(&backend->backends)) {
		struct subbackend_state *sub =
			wl_container_of(backend->backends.next, sub, link);
		wlr_backend_destroy(sub->backend);
	}

	// Destroy this backend only after removing all sub-backends
	wlr_signal_emit_safe(&wlr_backend->events.destroy, backend);
	free(backend);
}

static struct wlr_renderer *multi_backend_get_renderer(
		struct wlr_backend *backend) {
	struct wlr_multi_backend *multi = multi_backend_from_backend(backend);

	struct subbackend_state *sub;
	wl_list_for_each(sub, &multi->backends, link) {
		struct wlr_renderer *rend = wlr_backend_get_renderer(sub->backend);
		if (rend != NULL) {
			return rend;
		}
	}
	return NULL;
}

static struct wlr_session *multi_backend_get_session(
		struct wlr_backend *_backend) {
	struct wlr_multi_backend *backend = multi_backend_from_backend(_backend);
	return backend->session;
}

static clockid_t multi_backend_get_presentation_clock(
		struct wlr_backend *backend) {
	struct wlr_multi_backend *multi = multi_backend_from_backend(backend);

	struct subbackend_state *sub;
	wl_list_for_each(sub, &multi->backends, link) {
		if (sub->backend->impl->get_presentation_clock) {
			return wlr_backend_get_presentation_clock(sub->backend);
		}
	}

	return CLOCK_MONOTONIC;
}

struct wlr_backend_impl backend_impl = {
	.start = multi_backend_start,
	.destroy = multi_backend_destroy,
	.get_renderer = multi_backend_get_renderer,
	.get_session = multi_backend_get_session,
	.get_presentation_clock = multi_backend_get_presentation_clock,
};

static void handle_display_destroy(struct wl_listener *listener, void *data) {
	struct wlr_multi_backend *backend =
		wl_container_of(listener, backend, display_destroy);
	multi_backend_destroy((struct wlr_backend*)backend);
}

static void handle_add_gpu(struct wl_listener *listener, void *data) {
	struct wlr_multi_backend *multi =
			wl_container_of(listener, multi, hotplug.add_gpu_signal);
	struct wlr_event_add_gpu *event = data;

	wlr_log(WLR_INFO, "got handle_gpu signal with fd = %d", event->gpu_fd);

	// TODO: create_renderer_func should not be forced NULL
	//       even though this currently works in sway, it may not work in general.
	struct wlr_backend *child_drm = wlr_drm_backend_create(
			get_drm_backend_from_backend(multi->hotplug.primary_drm)->display,
			multi->session,
			event->gpu_fd,
			multi->hotplug.primary_drm,
			NULL);

	if (!child_drm) {
		wlr_log(WLR_ERROR, "Failed to open DRM device %d", event->gpu_fd);
		return;
	} else {
		wlr_log(WLR_INFO, "Successfully opened DRM device %d", event->gpu_fd);
	}

	if (!wlr_multi_backend_add(&multi->backend, child_drm)) {
		wlr_log(WLR_ERROR, "Failed to add new drm backend to multi backend");
	} else {
		wlr_log(WLR_ERROR, "NOTICE: successfully new drm backend to multi backend");
	}

	if(!wlr_backend_start(child_drm)) {
		wlr_log(WLR_ERROR, "Failed to start new child drm backend");
	}
}

struct handle_remove_specific_gpu {
	struct wlr_multi_backend *multi;
	struct wlr_event_remove_gpu *event;
	struct wlr_backend *to_remove;
};

static void handle_remove_specific_gpu(struct wlr_backend *backend, void *data) {
	struct handle_remove_specific_gpu *s = data;

	if(wlr_backend_is_drm(backend)) {
		struct wlr_drm_backend *drm = get_drm_backend_from_backend(backend);
		if(drm->fd == s->event->gpu_fd) {
			s->to_remove = backend;
		}
	}
}

static void handle_remove_gpu(struct wl_listener *listener, void *data) {
	struct handle_remove_specific_gpu s;
	s.multi =
			wl_container_of(listener, s.multi, hotplug.remove_gpu_signal);
	s.event = data;
	s.to_remove = NULL;

	wlr_log(WLR_INFO, "handling remove GPU!!");

	fprintf(stderr, "is MULTI? %d\n", wlr_backend_is_multi(&s.multi->backend));
	wlr_multi_for_each_backend(&s.multi->backend, handle_remove_specific_gpu, &s);

	if(s.to_remove) {
		wlr_log(WLR_DEBUG, "removing from multi backend");
		wlr_multi_backend_remove(&s.multi->backend, s.to_remove);
		wlr_log(WLR_DEBUG, "destroying backend");
		wlr_backend_destroy(s.to_remove);
	}
}

struct wlr_backend *wlr_multi_backend_create(struct wl_display *display) {
	struct wlr_multi_backend *backend =
		calloc(1, sizeof(struct wlr_multi_backend));
	if (!backend) {
		wlr_log(WLR_ERROR, "Backend allocation failed");
		return NULL;
	}

	wl_list_init(&backend->backends);
	wlr_backend_init(&backend->backend, &backend_impl);

	wl_signal_init(&backend->events.backend_add);
	wl_signal_init(&backend->events.backend_remove);

	backend->display_destroy.notify = handle_display_destroy;
	wl_display_add_destroy_listener(display, &backend->display_destroy);

	backend->hotplug.enabled = false;

	return &backend->backend;
}

void wlr_multi_backend_init_gpu_hotplug(struct wlr_multi_backend *multi, struct wlr_backend *primary_drm) {
	multi->hotplug.add_gpu_signal.notify = handle_add_gpu;
	wl_signal_add(&multi->session->events.add_gpu, &multi->hotplug.add_gpu_signal);
	multi->hotplug.remove_gpu_signal.notify = handle_remove_gpu;
	wl_signal_add(&multi->session->events.remove_gpu, &multi->hotplug.remove_gpu_signal);
	multi->hotplug.primary_drm = primary_drm;
	multi->hotplug.enabled = true;
}

bool wlr_backend_is_multi(struct wlr_backend *b) {
	return b->impl == &backend_impl;
}

static void new_input_reemit(struct wl_listener *listener, void *data) {
	struct subbackend_state *state = wl_container_of(listener,
			state, new_input);
	wlr_signal_emit_safe(&state->container->events.new_input, data);
}

static void new_output_reemit(struct wl_listener *listener, void *data) {
	struct subbackend_state *state = wl_container_of(listener,
			state, new_output);
	wlr_signal_emit_safe(&state->container->events.new_output, data);
}

static void handle_subbackend_destroy(struct wl_listener *listener,
		void *data) {
	struct subbackend_state *state = wl_container_of(listener, state, destroy);
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

bool wlr_multi_backend_add(struct wlr_backend *_multi,
		struct wlr_backend *backend) {
	struct wlr_multi_backend *multi = multi_backend_from_backend(_multi);

	if (multi_backend_get_subbackend(multi, backend)) {
		// already added
		return true;
	}

	struct wlr_renderer *multi_renderer =
		multi_backend_get_renderer(&multi->backend);
	struct wlr_renderer *backend_renderer = wlr_backend_get_renderer(backend);
	if (multi_renderer != NULL && backend_renderer != NULL && multi_renderer != backend_renderer) {
		wlr_log(WLR_ERROR, "Could not add backend: multiple renderers at the "
			"same time aren't supported");
		return false;
	}

	struct subbackend_state *sub = calloc(1, sizeof(struct subbackend_state));
	if (sub == NULL) {
		wlr_log(WLR_ERROR, "Could not add backend: allocation failed");
		return false;
	}
	wl_list_insert(multi->backends.prev, &sub->link);

	sub->backend = backend;
	sub->container = &multi->backend;

	wl_signal_add(&backend->events.destroy, &sub->destroy);
	sub->destroy.notify = handle_subbackend_destroy;

	wl_signal_add(&backend->events.new_input, &sub->new_input);
	sub->new_input.notify = new_input_reemit;

	wl_signal_add(&backend->events.new_output, &sub->new_output);
	sub->new_output.notify = new_output_reemit;

	wlr_signal_emit_safe(&multi->events.backend_add, backend);
	return true;
}

void wlr_multi_backend_remove(struct wlr_backend *_multi,
		struct wlr_backend *backend) {
	struct wlr_multi_backend *multi = multi_backend_from_backend(_multi);

	struct subbackend_state *sub =
		multi_backend_get_subbackend(multi, backend);

	if (sub) {
		wlr_signal_emit_safe(&multi->events.backend_remove, backend);
		subbackend_state_destroy(sub);
	}
}

bool wlr_multi_is_empty(struct wlr_backend *_backend) {
	assert(wlr_backend_is_multi(_backend));
	struct wlr_multi_backend *backend = (struct wlr_multi_backend *)_backend;
	return wl_list_length(&backend->backends) < 1;
}

void wlr_multi_for_each_backend(struct wlr_backend *_backend,
		void (*callback)(struct wlr_backend *backend, void *data), void *data) {
	assert(wlr_backend_is_multi(_backend));
	struct wlr_multi_backend *backend = (struct wlr_multi_backend *)_backend;
	struct subbackend_state *sub;
	wl_list_for_each(sub, &backend->backends, link) {
		callback(sub->backend, data);
	}
}
