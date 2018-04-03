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

/**
 * Automatically initializes the most suitable backend given the environment.
 * Will always return a multibackend. The backend is created but not started.
 * Returns NULL on failure.
 */
struct wlr_backend *wlr_backend_autocreate(struct wl_display *display);
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
 * Obtains the wlr_egl reference this backend is using.
 */
struct wlr_egl *wlr_backend_get_egl(struct wlr_backend *backend);
/**
 * Obtains the wlr_renderer reference this backend is using.
 */
struct wlr_renderer *wlr_backend_get_renderer(struct wlr_backend *backend);

#endif
