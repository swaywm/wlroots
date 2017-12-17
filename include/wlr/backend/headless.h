#ifndef WLR_BACKEND_HEADLESS_H
#define WLR_BACKEND_HEADLESS_H

#include <wlr/backend.h>
#include <wlr/types/wlr_input_device.h>

struct wlr_backend *wlr_headless_backend_create(struct wl_display *display);
struct wlr_output *wlr_headless_add_output(struct wlr_backend *backend,
	unsigned int width, unsigned int height);
struct wlr_input_device *wlr_headless_add_input(struct wlr_backend *backend,
	enum wlr_input_device_type type);
bool wlr_backend_is_headless(struct wlr_backend *backend);

#endif
