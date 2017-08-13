#ifndef _WLR_BACKEND_INTERFACE_H
#define _WLR_BACKEND_INTERFACE_H

#include <stdbool.h>
#include <wlr/backend.h>
#include <wlr/egl.h>

struct wlr_backend_impl {
	bool (*start)(struct wlr_backend *backend);
	void (*destroy)(struct wlr_backend *backend);
	struct wlr_egl *(*get_egl)(struct wlr_backend *backend);
};

void wlr_backend_init(struct wlr_backend *backend,
		const struct wlr_backend_impl *impl);

#endif
