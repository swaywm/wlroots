#ifndef BACKEND_H
#define BACKEND_H

#include <wlr/backend.h>

int backend_get_drm_fd(struct wlr_backend *backend);

#endif
