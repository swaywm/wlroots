#ifndef WLR_SESSION_H
#define WLR_SESSION_H

struct wlr_session;

struct wlr_session *wlr_session_start(void);
void wlr_session_finish(struct wlr_session *session);
int wlr_session_open_file(struct wlr_session *restrict session,
	const char *restrict path);
void wlr_session_close_file(struct wlr_session *session, int fd);

#endif
