#ifndef BACKEND_MULTI_H
#define BACKEND_MULTI_H

#include <wlr/backend/interface.h>
#include <wlr/backend/multi.h>
#include <wlr/util/list.h>
#include <wlr/backend/session.h>

struct wlr_multi_backend {
	struct wlr_backend backend;

	struct wlr_session *session;
	list_t *backends;
};

#endif
