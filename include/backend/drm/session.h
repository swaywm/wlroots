#ifndef SESSION_H
#define SESSION_H

#include <systemd/sd-bus.h>
#include <stdbool.h>

struct wlr_session {
	sd_bus *bus;

	char *id;
	char *path;
	char *seat;
};

bool wlr_session_start(struct wlr_session *session);
void wlr_session_end(struct wlr_session *session);

int wlr_session_take_device(struct wlr_session *restrict session,
		const char *restrict path,
		bool *restrict paused_out);

void wlr_session_release_device(struct wlr_session *session, int fd);

#endif
