#ifndef WLR_BACKEND_X11_H
#define WLR_BACKEND_X11_H

#include <wlr/backend.h>
#include <stdbool.h>

struct wlr_backend *wlr_x11_backend_create(const char *display);

bool wlr_backend_is_x11(struct wlr_backend *backend);

#endif
