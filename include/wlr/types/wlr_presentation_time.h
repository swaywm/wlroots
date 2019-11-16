/*
 * This an unstable interface of wlroots. No guarantees are made regarding the
 * future consistency of this API.
 */
#ifndef WLR_USE_UNSTABLE
#error "Add -DWLR_USE_UNSTABLE to enable unstable wlroots features"
#endif

#ifndef WLR_TYPES_WLR_PRESENTATION_TIME_H
#define WLR_TYPES_WLR_PRESENTATION_TIME_H

#include <stdbool.h>
#include <stddef.h>
#include <time.h>
#include <wayland-server-core.h>

struct wlr_presentation {
	struct wl_global *global;
	struct wl_list resources; // wl_resource_get_link
	struct wl_list feedbacks; // wlr_presentation_feedback::link
	clockid_t clock;

	struct {
		struct wl_signal destroy;
	} events;

	struct wl_listener display_destroy;
};

struct wlr_presentation_feedback {
	struct wlr_presentation *presentation;
	struct wlr_surface *surface; // NULL if the surface has been destroyed
	struct wl_list link; // wlr_presentation::feedbacks

	struct wl_list resources; // wl_resource_get_link

	// The surface contents were committed.
	bool committed;
	// The surface contents were sampled by the compositor and are to be
	// presented on the next flip. Can become true only after committed becomes
	// true.
	bool sampled;
	bool presented;

	struct wl_listener surface_commit;
	struct wl_listener surface_destroy;
};

struct wlr_presentation_event {
	struct wlr_output *output;
	uint64_t tv_sec;
	uint32_t tv_nsec;
	uint32_t refresh;
	uint64_t seq;
	uint32_t flags; // wp_presentation_feedback_kind
};

struct wlr_backend;

struct wlr_presentation *wlr_presentation_create(struct wl_display *display,
	struct wlr_backend *backend);
void wlr_presentation_destroy(struct wlr_presentation *presentation);
/**
 * Mark the current surface's buffer as sampled.
 *
 * The compositor must call this function when it uses the surface's current
 * contents (e.g. when rendering the surface's current texture, when
 * referencing its current buffer, or when directly scanning out its current
 * buffer). A wlr_presentation_feedback is returned. The compositor should call
 * wlr_presentation_feedback_send_presented if this content has been displayed,
 * then wlr_presentation_feedback_destroy.
 *
 * NULL is returned if the client hasn't requested presentation feedback for
 * this surface.
 */
struct wlr_presentation_feedback *wlr_presentation_surface_sampled(
	struct wlr_presentation *presentation, struct wlr_surface *surface);
void wlr_presentation_feedback_send_presented(
	struct wlr_presentation_feedback *feedback,
	struct wlr_presentation_event *event);
void wlr_presentation_feedback_destroy(
	struct wlr_presentation_feedback *feedback);

#endif
