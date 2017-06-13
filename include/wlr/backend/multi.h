#ifndef _WLR_BACKEND_MULTI_H
#define _WLR_BACKEND_MULTI_H

#include <wlr/backend.h>

struct wlr_backend *wlr_multi_backend_create();
void wlr_multi_backend_add(struct wlr_backend *multi,
		struct wlr_backend *backend);

#endif
