/*
 * This an unstable interface of wlroots. No guarantees are made regarding the
 * future consistency of this API.
 */
#ifndef WLR_USE_UNSTABLE
#error "Add -DWLR_USE_UNSTABLE to enable unstable wlroots features"
#endif

#ifndef WLR_BACKEND_NOOP_H
#define WLR_BACKEND_NOOP_H

#include <wlr/backend.h>
#include <wlr/types/wlr_output.h>

/**
 * Creates a noop backend. Noop backends do not have a framebuffer and are not
 * capable of rendering anything. They are useful for when there's no real
 * outputs connected; you can stash your views on a noop output until an output
 * is connected.
 */
struct wlr_backend *wlr_noop_backend_create(struct wl_display *display);

/**
 * Create a new noop output.
 */
struct wlr_output *wlr_noop_add_output(struct wlr_backend *backend);

bool wlr_backend_is_noop(struct wlr_backend *backend);
bool wlr_output_is_noop(struct wlr_output *output);

#endif
