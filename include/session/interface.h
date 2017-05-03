#ifndef WLR_SESSION_INTERFACE_H
#define WLR_SESSION_INTERFACE_H

struct wlr_session;

struct session_interface {
	struct wlr_session *(*start)(void);
	void (*finish)(struct wlr_session *session);
	int (*open)(struct wlr_session *restrict session,
		const char *restrict path);
	void (*close)(struct wlr_session *session, int fd);
};

struct wlr_session {
	struct session_interface iface;
};

extern const struct session_interface session_logind_iface;

#endif
