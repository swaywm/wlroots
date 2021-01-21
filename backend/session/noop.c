#define _POSIX_C_SOURCE 200809L
#include <fcntl.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wlr/backend/session/interface.h>
#include <wlr/util/log.h>
#include "backend/session/session.h"
#include "util/signal.h"

const struct session_impl session_noop;

static int noop_session_open(struct wlr_session *base, const char *path) {
	return open(path, O_RDWR | O_CLOEXEC);
}

static void noop_session_close(struct wlr_session *base, int fd) {
	close(fd);
}

static bool noop_change_vt(struct wlr_session *base, unsigned vt) {
	return false;
}

static void noop_session_destroy(struct wlr_session *base) {
	free(base);
}

static struct wlr_session *noop_session_create(struct wl_display *disp) {
	struct wlr_session *session = calloc(1, sizeof(*session));
	if (!session) {
		wlr_log_errno(WLR_ERROR, "Allocation failed");
		return NULL;
	}

	session_init(session);
	session->impl = &session_noop;
	session->active = true;

	wlr_log(WLR_INFO, "Successfully initialized noop session");
	return session;
}

const struct session_impl session_noop = {
	.create = noop_session_create,
	.destroy = noop_session_destroy,
	.open = noop_session_open,
	.close = noop_session_close,
	.change_vt = noop_change_vt,
};
