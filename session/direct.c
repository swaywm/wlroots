#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <wayland-server.h>

#include "session/interface.h"
#include "common/log.h"

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

static void direct_session_finish(struct wlr_session *base) {
	struct direct_session *session = wl_container_of(base, session, base);

	free(session);
}

static struct wlr_session *direct_session_start(void) {
	struct direct_session *session = calloc(1, sizeof(*session));
	if (!session) {
		wlr_log(L_ERROR, "Allocation failed: %s", strerror(errno));
		return NULL;
	}

	wlr_log(L_INFO, "Successfully loaded direct session");

	session->base.iface = session_direct_iface;
	return &session->base;
}

const struct session_interface session_direct_iface = {
	.start = direct_session_start,
	.finish = direct_session_finish,
	.open = direct_session_open,
	.close = direct_session_close,
};
