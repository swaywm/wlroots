#ifndef BACKEND_WLR_BACKEND_H
#define BACKEND_WLR_BACKEND_H

#include <wlr/backend.h>

/**
 * Get the supported buffer capabilities.
 *
 * This functions returns a bitfield of supported wlr_buffer_cap.
 */
uint32_t backend_get_buffer_caps(struct wlr_backend *backend);

/**
 * Get the backend's allocator. Automatically creates the allocator if
 * necessary.
 */
struct wlr_allocator *backend_get_allocator(struct wlr_backend *backend);

#endif
