#ifndef WLR_SESSION_INTERFACE_H
#define WLR_SESSION_INTERFACE_H

struct wlr_session;

struct session_interface {
	struct wlr_session *(*start)(struct wl_display *disp);
	void (*finish)(struct wlr_session *session);
	int (*open)(struct wlr_session *restrict session,
		const char *restrict path);
	void (*close)(struct wlr_session *session, int fd);
	bool (*change_vt)(struct wlr_session *session, int vt);
};

struct wlr_session {
	struct session_interface iface;
};

extern const struct session_interface session_logind_iface;
extern const struct session_interface session_direct_iface;

#endif
