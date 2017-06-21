#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <wayland-server.h>
#include <wlr/session/interface.h>
#include <wlr/util/log.h>

const struct session_impl session_direct;

struct direct_session {
	struct wlr_session base;
};

static int direct_session_open(struct wlr_session *restrict base,
	const char *restrict path) {
	return open(path, O_RDWR | O_CLOEXEC);
}

static void direct_session_close(struct wlr_session *base, int fd) {
	close(fd);
}

static bool direct_change_vt(struct wlr_session *base, int vt) {
	// TODO
	return false;
}

static void direct_session_finish(struct wlr_session *base) {
	struct direct_session *session = wl_container_of(base, session, base);

	free(session);
}

static struct wlr_session *direct_session_start(struct wl_display *disp) {
	struct direct_session *session = calloc(1, sizeof(*session));
	if (!session) {
		wlr_log(L_ERROR, "Allocation failed: %s", strerror(errno));
		return NULL;
	}

	wlr_log(L_INFO, "Successfully loaded direct session");

	session->base.impl = &session_direct;
	session->base.active = true;
	wl_signal_init(&session->base.session_signal);
	return &session->base;
}

const struct session_impl session_direct = {
	.start = direct_session_start,
	.finish = direct_session_finish,
	.open = direct_session_open,
	.close = direct_session_close,
	.change_vt = direct_change_vt,
};
