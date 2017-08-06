#ifndef WLR_SESSION_INTERFACE_H
#define WLR_SESSION_INTERFACE_H

#include <wlr/backend/session.h>

struct session_impl {
	struct wlr_session *(*start)(struct wl_display *disp);
	void (*finish)(struct wlr_session *session);
	int (*open)(struct wlr_session *session, const char *path);
	void (*close)(struct wlr_session *session, int fd);
	bool (*change_vt)(struct wlr_session *session, unsigned vt);
};

#endif
