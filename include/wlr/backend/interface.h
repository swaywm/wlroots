#ifndef _WLR_BACKEND_INTERFACE_H
#define _WLR_BACKEND_INTERFACE_H

#include <stdbool.h>
#include <wlr/backend.h>
#include <wlr/egl.h>

struct wlr_backend_state;

struct wlr_backend_impl {
	bool (*init)(struct wlr_backend_state *state);
	void (*destroy)(struct wlr_backend_state *state);
	struct wlr_egl *(*get_egl)(struct wlr_backend_state *state);
};

struct wlr_backend *wlr_backend_create(const struct wlr_backend_impl *impl,
		struct wlr_backend_state *state);

#endif
