#ifndef _WLR_BACKEND_H
#define _WLR_BACKEND_H

struct wlr_backend *wlr_backend_init();
void wlr_backend_free(struct wlr_backend *backend);

#endif
