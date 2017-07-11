#ifndef _WLR_BACKEND_MULTI_H
#define _WLR_BACKEND_MULTI_H

#include <wlr/backend.h>
#include <wlr/backend/udev.h>
#include <wlr/backend/session.h>

struct wlr_backend *wlr_multi_backend_create(struct wlr_session *session,
		struct wlr_udev *udev);
void wlr_multi_backend_add(struct wlr_backend *multi,
		struct wlr_backend *backend);

#endif
