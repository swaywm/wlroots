#ifndef WLR_BACKEND_MULTI_H
#define WLR_BACKEND_MULTI_H

#include <wlr/backend.h>
#include <wlr/backend/session.h>

struct wlr_backend *wlr_multi_backend_create(struct wlr_session *session);
void wlr_multi_backend_add(struct wlr_backend *multi,
		struct wlr_backend *backend);

bool wlr_backend_is_multi(struct wlr_backend *backend);

struct wlr_session *wlr_multi_get_session(struct wlr_backend *base);

#endif
