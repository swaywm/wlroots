#ifndef _WLR_MULTI_BACKEND_INTERNAL
#define _WLR_MULTI_BACKEND_INTERNAL

#include <wlr/backend/interface.h>
#include <wlr/backend/multi.h>
#include <wlr/util/list.h>

struct wlr_backend_state {
	struct wlr_backend *backend;
	list_t *backends;
};

#endif
