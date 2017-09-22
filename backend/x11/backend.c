#include <stdbool.h>
#include <wlr/backend/interface.h>
#include <wlr/backend/x11.h>
#include <wlr/egl.h>
#include "backend/x11.h"

struct wlr_backend *wlr_x11_backend_create(const char *display) {
	return NULL;
}

static bool wlr_x11_backend_start(struct wlr_backend *backend) {
	return false;
}

static void wlr_x11_backend_destroy(struct wlr_backend *backend) {
}

struct wlr_egl *wlr_x11_backend_get_egl(struct wlr_backend *backend) {
	return NULL;
}

static struct wlr_backend_impl backend_impl = {
	.start = wlr_x11_backend_start,
	.destroy = wlr_x11_backend_destroy,
	.get_egl = wlr_x11_backend_get_egl,
};

bool wlr_backend_is_x11(struct wlr_backend *backend) {
	return backend->impl == &backend_impl;
}
