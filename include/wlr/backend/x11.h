#ifndef WLR_BACKEND_X11_H
#define WLR_BACKEND_X11_H

#include <stdbool.h>

#include <wayland-server.h>

#include <wlr/backend.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_output.h>

struct wlr_backend *wlr_x11_backend_create(struct wl_display *display,
	const char *x11_display, wlr_renderer_create_func_t create_renderer_func);
struct wlr_output *wlr_x11_output_create(struct wlr_backend *backend);

bool wlr_backend_is_x11(struct wlr_backend *backend);
bool wlr_input_device_is_x11(struct wlr_input_device *device);
bool wlr_output_is_x11(struct wlr_output *output);

#endif
