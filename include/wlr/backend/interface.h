#ifndef WLR_BACKEND_INTERFACE_H
#define WLR_BACKEND_INTERFACE_H

#include <stdbool.h>
#include <wlr/backend.h>
#include <wlr/render/egl.h>

struct wlr_backend_impl {
	bool (*start)(struct wlr_backend *backend);
	void (*destroy)(struct wlr_backend *backend);
	struct wlr_renderer *(*get_renderer)(struct wlr_backend *backend);
};

/**
 * Initializes common state on a wlr_backend and sets the implementation to the
 * provided wlr_backend_impl reference.
 */
void wlr_backend_init(struct wlr_backend *backend,
		const struct wlr_backend_impl *impl);

#endif
