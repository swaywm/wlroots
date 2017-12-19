#ifndef WLR_BACKEND_WAYLAND_H
#define WLR_BACKEND_WAYLAND_H

#include <wayland-client.h>
#include <wayland-server.h>
#include <wlr/backend.h>
#include <wlr/types/wlr_output.h>
#include <stdbool.h>

/**
 * Creates a new wlr_wl_backend. This backend will be created with no outputs;
 * you must use wlr_wl_output_create to add them.
 */
struct wlr_backend *wlr_wl_backend_create(struct wl_display *display);

/**
 * Adds a new output to this backend. You may remove outputs by destroying them.
 * Note that if called before initializing the backend, this will return NULL
 * and your outputs will be created during initialization (and given to you via
 * the output_add signal).
 */
struct wlr_output *wlr_wl_output_create(struct wlr_backend *backend);

/**
 * True if the given backend is a wlr_wl_backend.
 */
bool wlr_backend_is_wl(struct wlr_backend *backend);

/**
 * True if the given output is a wlr_wl_backend_output.
 */
bool wlr_output_is_wl(struct wlr_output *output);

#endif
