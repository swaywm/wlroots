/*
 * This an unstable interface of wlroots. No guarantees are made regarding the
 * future consistency of this API.
 */
#ifndef WLR_USE_UNSTABLE
#error "Add -DWLR_USE_UNSTABLE to enable unstable wlroots features"
#endif

#ifndef WLR_BACKEND_H
#define WLR_BACKEND_H

#include <wayland-server.h>
#include <wlr/backend/session.h>
#include <wlr/render/egl.h>

struct wlr_backend_impl;

struct wlr_backend {
	const struct wlr_backend_impl *impl;

	struct {
		/** Raised when destroyed, passed the wlr_backend reference */
		struct wl_signal destroy;
		/** Raised when new inputs are added, passed the wlr_input_device */
		struct wl_signal new_input;
		/** Raised when new outputs are added, passed the wlr_output */
		struct wl_signal new_output;
	} events;
};

typedef struct wlr_renderer *(*wlr_renderer_create_func_t)(
	struct wlr_backend *backend);
/**
 * Automatically initializes the most suitable backend given the environment.
 * Will always return a multibackend. The backend is created but not started.
 * Returns NULL on failure.
 *
 * The compositor can request to initialize the backend's renderer by setting
 * the create_render_func. The callback must initialize the given wlr_egl and
 * return a valid wlr_renderer, or NULL if it has failed to initiaze it.
 * Pass NULL as create_renderer_func to use the backend's default renderer.
 */
struct wlr_backend *wlr_backend_autocreate(struct wl_display *display,
	wlr_renderer_create_func_t create_renderer_func);
/**
 * Start the backend. This may signal new_input or new_output immediately, but
 * may also wait until the display's event loop begins. Returns false on
 * failure.
 */
bool wlr_backend_start(struct wlr_backend *backend);
/**
 * Destroy the backend and clean up all of its resources. Normally called
 * automatically when the wl_display is destroyed.
 */
void wlr_backend_destroy(struct wlr_backend *backend);
/**
 * Obtains the wlr_renderer reference this backend is using.
 */
struct wlr_renderer *wlr_backend_get_renderer(struct wlr_backend *backend);
/**
 * Obtains the wlr_session reference from this backend if there is any.
 * Might return NULL for backends that don't use a session.
 */
struct wlr_session *wlr_backend_get_session(struct wlr_backend *backend);
/**
 * Returns the clock used by the backend for presentation feedback.
 */
clockid_t wlr_backend_get_presentation_clock(struct wlr_backend *backend);

#endif
