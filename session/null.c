#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <wayland-server.h>
#include <wayland-util.h>
#include <wlr/session/interface.h>
#include <wlr/util/log.h>

const struct session_impl session_null;

static bool null_session_change_vt(struct wlr_session *session, unsigned vt) {
	return false;
}

static void null_session_close(struct wlr_session *session, int fd) {
	// Calling this function is a programming error
	wlr_log(L_ERROR, "Invalid call to wlr_session_close with null session");
	exit(1);
}

static int null_session_open(struct wlr_session *session, const char *path) {
	// Calling this function is a programming error
	wlr_log(L_ERROR, "Invalid call to wlr_session_open with null session");
	exit(1);
}

static void null_session_finish(struct wlr_session *session) {
	free(session);
}

static struct wlr_session *null_session_start(struct wl_display *disp) {
	if (!getenv("WAYLAND_DISPLAY") && !getenv("_WAYLAND_DISPLAY") && !getenv("DISPLAY")) {
		return NULL;
	}

	struct wlr_session *session = calloc(1, sizeof(*session));
	if (!session) {
		wlr_log_errno(L_ERROR, "Allocation failed");
		return NULL;
	}

	const char *seat = getenv("XDG_SEAT");
	if (!seat) {
		seat = "seat0";
	}

	wlr_log(L_INFO, "Successfully loaded null session");

	snprintf(session->seat, sizeof(session->seat), "%s", seat);
	session->drm_fd = -1;
	session->impl = &session_null;
	session->active = true;
	wl_signal_init(&session->session_signal);
	return session;
}

const struct session_impl session_null = {
	.start = null_session_start,
	.finish = null_session_finish,
	.open = null_session_open,
	.close = null_session_close,
	.change_vt = null_session_change_vt,
};
