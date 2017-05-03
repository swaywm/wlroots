#include <stddef.h>

#include <wlr/session.h>
#include <stdarg.h>
#include "common/log.h"
#include "session/interface.h"

static const struct session_interface *ifaces[] = {
#ifdef HAS_SYSTEMD
	&session_logind_iface,
#endif
	NULL,
};

struct wlr_session *wlr_session_start(void) {
	const struct session_interface **iter;

	for (iter = ifaces; *iter; ++iter) {
		struct wlr_session *session = (*iter)->start();
		if (session) {
			return session;
		}
	}

	wlr_log(L_ERROR, "Failed to load session backend");
	return NULL;
}

void wlr_session_finish(struct wlr_session *session) {
	session->iface.finish(session);
};

int wlr_session_open_file(struct wlr_session *restrict session,
	const char *restrict path) {

	return session->iface.open(session, path);
}

void wlr_session_close_file(struct wlr_session *session, int fd) {
	session->iface.close(session, fd);
}
