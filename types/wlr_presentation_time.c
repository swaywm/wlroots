#define _POSIX_C_SOURCE 199309L
#include <assert.h>
#include <stdlib.h>
#include <wlr/types/wlr_presentation_time.h>
#include <wlr/types/wlr_surface.h>
#include <wlr/backend.h>
#include "presentation-time-protocol.h"
#include "util/signal.h"

#define PRESENTATION_VERSION 1

static struct wlr_presentation_feedback *presentation_feedback_from_resource(
		struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource,
		&wp_presentation_feedback_interface, NULL));
	return wl_resource_get_user_data(resource);
}

static void feedback_handle_resource_destroy(struct wl_resource *resource) {
	struct wlr_presentation_feedback *feedback =
		presentation_feedback_from_resource(resource);
	wl_list_remove(&feedback->surface_commit.link);
	wl_list_remove(&feedback->surface_destroy.link);
	wl_list_remove(&feedback->link);
	free(feedback);
}

// Destroys the feedback
static void feedback_send_presented(struct wlr_presentation_feedback *feedback,
		struct wlr_presentation_event *event) {
	struct wl_client *client = wl_resource_get_client(feedback->resource);
	struct wl_resource *resource;
	wl_resource_for_each(resource, &event->output->resources) {
		if (wl_resource_get_client(resource) == client) {
			wp_presentation_feedback_send_sync_output(feedback->resource,
				resource);
		}
	}

	uint32_t tv_sec_hi = event->tv_sec >> 32;
	uint32_t tv_sec_lo = event->tv_sec & 0xFFFFFFFF;
	uint32_t seq_hi = event->seq >> 32;
	uint32_t seq_lo = event->seq & 0xFFFFFFFF;
	wp_presentation_feedback_send_presented(feedback->resource,
		tv_sec_hi, tv_sec_lo, event->tv_nsec, event->refresh,
		seq_hi, seq_lo, event->flags);

	wl_resource_destroy(feedback->resource);
}

// Destroys the feedback
static void feedback_send_discarded(
		struct wlr_presentation_feedback *feedback) {
	wp_presentation_feedback_send_discarded(feedback->resource);
	wl_resource_destroy(feedback->resource);
}

static void feedback_handle_surface_commit(struct wl_listener *listener,
		void *data) {
	struct wlr_presentation_feedback *feedback =
		wl_container_of(listener, feedback, surface_commit);

	if (feedback->committed) {
		// The content update has been superseded
		feedback_send_discarded(feedback);
	} else {
		feedback->committed = true;
	}
}

static void feedback_handle_surface_destroy(struct wl_listener *listener,
		void *data) {
	struct wlr_presentation_feedback *feedback =
		wl_container_of(listener, feedback, surface_destroy);
	feedback_send_discarded(feedback);
}

static const struct wp_presentation_interface presentation_impl;

static struct wlr_presentation *presentation_from_resource(
		struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource, &wp_presentation_interface,
		&presentation_impl));
	return wl_resource_get_user_data(resource);
}

static void presentation_handle_feedback(struct wl_client *client,
		struct wl_resource *resource, struct wl_resource *surface_resource,
		uint32_t id) {
	struct wlr_presentation *presentation =
		presentation_from_resource(resource);
	struct wlr_surface *surface = wlr_surface_from_resource(surface_resource);

	struct wlr_presentation_feedback *feedback =
		calloc(1, sizeof(struct wlr_presentation_feedback));
	if (feedback == NULL) {
		wl_client_post_no_memory(client);
		return;
	}

	uint32_t version = wl_resource_get_version(resource);
	feedback->resource = wl_resource_create(client,
		&wp_presentation_feedback_interface, version, id);
	if (feedback->resource == NULL) {
		free(feedback);
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(feedback->resource, NULL, feedback,
		feedback_handle_resource_destroy);

	feedback->surface = surface;

	feedback->surface_commit.notify = feedback_handle_surface_commit;
	wl_signal_add(&surface->events.commit, &feedback->surface_commit);

	feedback->surface_destroy.notify = feedback_handle_surface_destroy;
	wl_signal_add(&surface->events.destroy, &feedback->surface_destroy);

	wl_list_insert(&presentation->feedbacks, &feedback->link);
}

static void presentation_handle_destroy(struct wl_client *client,
		struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static const struct wp_presentation_interface presentation_impl = {
	.feedback = presentation_handle_feedback,
	.destroy = presentation_handle_destroy,
};

static void presentation_handle_resource_destroy(struct wl_resource *resource) {
	wl_list_remove(wl_resource_get_link(resource));
}

static void presentation_bind(struct wl_client *client, void *data,
		uint32_t version, uint32_t id) {
	struct wlr_presentation *presentation = data;

	struct wl_resource *resource = wl_resource_create(client,
		&wp_presentation_interface, version, id);
	if (resource == NULL) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(resource, &presentation_impl, presentation,
		presentation_handle_resource_destroy);
	wl_list_insert(&presentation->resources, wl_resource_get_link(resource));

	wp_presentation_send_clock_id(resource, (uint32_t)presentation->clock);
}

static void handle_display_destroy(struct wl_listener *listener, void *data) {
	struct wlr_presentation *presentation =
		wl_container_of(listener, presentation, display_destroy);
	wlr_presentation_destroy(presentation);
}

struct wlr_presentation *wlr_presentation_create(struct wl_display *display,
		struct wlr_backend *backend) {
	struct wlr_presentation *presentation =
		calloc(1, sizeof(struct wlr_presentation));
	if (presentation == NULL) {
		return NULL;
	}

	presentation->global = wl_global_create(display, &wp_presentation_interface,
		PRESENTATION_VERSION, presentation, presentation_bind);
	if (presentation->global == NULL) {
		free(presentation);
		return NULL;
	}

	presentation->clock = wlr_backend_get_presentation_clock(backend);

	wl_list_init(&presentation->resources);
	wl_list_init(&presentation->feedbacks);
	wl_signal_init(&presentation->events.destroy);

	presentation->display_destroy.notify = handle_display_destroy;
	wl_display_add_destroy_listener(display, &presentation->display_destroy);

	return presentation;
}

void wlr_presentation_destroy(struct wlr_presentation *presentation) {
	if (presentation == NULL) {
		return;
	}

	wlr_signal_emit_safe(&presentation->events.destroy, presentation);

	wl_global_destroy(presentation->global);

	struct wlr_presentation_feedback *feedback, *feedback_tmp;
	wl_list_for_each_safe(feedback, feedback_tmp, &presentation->feedbacks,
			link) {
		wl_resource_destroy(feedback->resource);
	}

	struct wl_resource *resource, *resource_tmp;
	wl_resource_for_each_safe(resource, resource_tmp,
			&presentation->resources) {
		wl_resource_destroy(resource);
	}

	wl_list_remove(&presentation->display_destroy.link);
	free(presentation);
}

void wlr_presentation_send_surface_presented(
		struct wlr_presentation *presentation, struct wlr_surface *surface,
		struct wlr_presentation_event *event) {
	// TODO: maybe use a hashtable to optimize this function
	struct wlr_presentation_feedback *feedback, *feedback_tmp;
	wl_list_for_each_safe(feedback, feedback_tmp,
			&presentation->feedbacks, link) {
		if (feedback->surface == surface && feedback->committed) {
			feedback_send_presented(feedback, event);
		}
	}
}
