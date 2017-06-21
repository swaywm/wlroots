#include <stddef.h>
#include <stdarg.h>
#include <wlr/session.h>
#include <wlr/session/interface.h>
#include <wlr/util/log.h>

extern const struct session_impl session_logind;
extern const struct session_impl session_direct;

static const struct session_impl *impls[] = {
#ifdef HAS_SYSTEMD
	&session_logind,
#endif
	&session_direct,
	NULL,
};

struct wlr_session *wlr_session_start(struct wl_display *disp) {
	const struct session_impl **iter;

	for (iter = impls; *iter; ++iter) {
		struct wlr_session *session = (*iter)->start(disp);
		if (session) {
			return session;
		}
	}

	wlr_log(L_ERROR, "Failed to load session backend");
	return NULL;
}

void wlr_session_finish(struct wlr_session *session) {
	session->impl->finish(session);
};

int wlr_session_open_file(struct wlr_session *restrict session,
	const char *restrict path) {

	return session->impl->open(session, path);
}

void wlr_session_close_file(struct wlr_session *session, int fd) {
	session->impl->close(session, fd);
}

bool wlr_session_change_vt(struct wlr_session *session, int vt) {
	return session->impl->change_vt(session, vt);
}
