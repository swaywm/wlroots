#include <stddef.h>

#include <wlr/session.h>
#include <stdarg.h>
#include "common/log.h"
#include "session/interface.h"

static const struct session_interface *ifaces[] = {
#ifdef HAS_SYSTEMD
	&session_logind_iface,
#endif
	&session_direct_iface,
	NULL,
};

struct wlr_session *wlr_session_start(struct wl_display *disp) {
	const struct session_interface **iter;

	for (iter = ifaces; *iter; ++iter) {
		struct wlr_session *session = (*iter)->start(disp);
		if (session) {
			return session;
		}
	}

	wlr_log(L_ERROR, "Failed to load session backend");
	return NULL;
}

void wlr_session_finish(struct wlr_session *session) {
	session->iface->finish(session);
};

int wlr_session_open_file(struct wlr_session *restrict session,
	const char *restrict path) {

	return session->iface->open(session, path);
}

void wlr_session_close_file(struct wlr_session *session, int fd) {
	session->iface->close(session, fd);
}

bool wlr_session_change_vt(struct wlr_session *session, int vt) {
	return session->iface->change_vt(session, vt);
}
