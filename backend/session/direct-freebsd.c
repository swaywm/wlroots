#include <assert.h>
#include <linux/input.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/consio.h>
#include <sys/ioctl.h>
#include <sys/kbio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wlr/backend/session/interface.h>
#include <wlr/util/log.h>
#include <xf86drm.h>
#include "backend/session/direct-ipc.h"
#include "backend/session/session.h"
#include "util/signal.h"

const struct session_impl session_direct;

struct direct_session {
	struct wlr_session base;
	int tty_fd;
	int old_tty;
	int old_kbmode;
	int sock;
	pid_t child;

	struct wl_event_source *vt_source;
};

static struct direct_session *direct_session_from_session(
		struct wlr_session *base) {
	assert(base->impl == &session_direct);
	return (struct direct_session *)base;
}

static int direct_session_open(struct wlr_session *base, const char *path) {
	struct direct_session *session = direct_session_from_session(base);

	int fd = direct_ipc_open(session->sock, path);
	if (fd < 0) {
		wlr_log(WLR_ERROR, "Failed to open %s: %s%s", path, strerror(-fd),
			fd == -EINVAL ? "; is another display server running?" : "");
		return fd;
	}

	return fd;
}

static void direct_session_close(struct wlr_session *base, int fd) {
	struct direct_session *session = direct_session_from_session(base);

	int ev;
	struct drm_version dv = {0};
	if (ioctl(fd, DRM_IOCTL_VERSION, &dv) == 0) {
		direct_ipc_dropmaster(session->sock, fd);
	} else if (ioctl(fd, EVIOCGVERSION, &ev) == 0) {
		ioctl(fd, EVIOCREVOKE, 0);
	}

	close(fd);
}

static bool direct_change_vt(struct wlr_session *base, unsigned vt) {
	struct direct_session *session = direct_session_from_session(base);

	// Only seat0 has VTs associated with it
	if (strcmp(session->base.seat, "seat0") != 0) {
		return true;
	}

	return ioctl(session->tty_fd, VT_ACTIVATE, (int)vt) == 0;
}

static void direct_session_destroy(struct wlr_session *base) {
	struct direct_session *session = direct_session_from_session(base);

	if (strcmp(session->base.seat, "seat0") == 0) {
		struct vt_mode mode = {
			.mode = VT_AUTO,
		};

		errno = 0;

		ioctl(session->tty_fd, KDSKBMODE, session->old_kbmode);
		ioctl(session->tty_fd, KDSETMODE, KD_TEXT);
		ioctl(session->tty_fd, VT_SETMODE, &mode);

		ioctl(session->tty_fd, VT_ACTIVATE, session->old_tty);

		if (errno) {
			wlr_log(WLR_ERROR, "Failed to restore tty");
		}

		wl_event_source_remove(session->vt_source);
		close(session->tty_fd);
	}

	direct_ipc_finish(session->sock, session->child);
	close(session->sock);

	free(session);
}

static int vt_handler(int signo, void *data) {
	struct direct_session *session = data;
	struct drm_version dv = {0};
	struct wlr_device *dev;

	if (session->base.active) {
		session->base.active = false;
		wlr_signal_emit_safe(&session->base.events.active, NULL);

		wl_list_for_each(dev, &session->base.devices, link) {
			if (ioctl(dev->fd, DRM_IOCTL_VERSION, &dv) == 0) {
				direct_ipc_dropmaster(session->sock, dev->fd);
			}
		}

		ioctl(session->tty_fd, VT_RELDISP, 1);
	} else {
		ioctl(session->tty_fd, VT_RELDISP, VT_ACKACQ);

		wl_list_for_each(dev, &session->base.devices, link) {
			if (ioctl(dev->fd, DRM_IOCTL_VERSION, &dv) == 0) {
				direct_ipc_setmaster(session->sock, dev->fd);
			}
		}

		session->base.active = true;
		wlr_signal_emit_safe(&session->base.events.active, NULL);
	}

	return 1;
}

static bool get_tty_path(int tty, char path[static 11], size_t len) {
	assert(tty > 0);

	const char prefix[] = "/dev/ttyv";
	static_assert(sizeof(prefix) + 1 <= 11, "TTY path prefix is too long");

	const size_t prefix_len = sizeof(prefix) - 1;
	strcpy(path, prefix);

	size_t offset = prefix_len;
	const int num = tty - 1;
	if (num == 0) {
		path[offset++] = '0';
		path[offset++] = '\0';
		return true;
	}

	const int base = 32;
	for (int remaning = num; remaning > 0; remaning /= base, offset++) {
		// Return early if the buffer is too small.
		if (offset + 1 >= len) {
			return false;
		}

		const int value = remaning % base;
		if (value >= 10) {
			path[offset] = 'a' + value - 10;
		} else {
			path[offset] = '0' + value;
		}
	}

	const size_t num_len = offset - prefix_len;
	for (size_t i = 0; i < num_len / 2; i++) {
		const size_t p1 = prefix_len + i;
		const size_t p2 = offset - 1 - i;
		const char tmp = path[p1];
		path[p1] = path[p2];
		path[p2] = tmp;
	}

	path[offset++] = '\0';
	return true;
}

static bool setup_tty(struct direct_session *session, struct wl_display *display) {
	int fd = -1, tty = -1, tty0_fd = -1, old_tty = 1;
	if ((tty0_fd = open("/dev/ttyv0", O_RDWR | O_CLOEXEC)) < 0) {
		wlr_log_errno(WLR_ERROR, "Could not open /dev/ttyv0 to find a free vt");
		goto error;
	}
	if (ioctl(tty0_fd, VT_GETACTIVE, &old_tty) != 0) {
		wlr_log_errno(WLR_ERROR, "Could not get active vt");
		goto error;
	}
	if (ioctl(tty0_fd, VT_OPENQRY, &tty) != 0) {
		wlr_log_errno(WLR_ERROR, "Could not find a free vt");
		goto error;
	}
	close(tty0_fd);
	char tty_path[64];
	if (!get_tty_path(tty, tty_path, sizeof(tty_path))) {
		wlr_log(WLR_ERROR, "Could not get tty %d path", tty);
		goto error;
	}
	wlr_log(WLR_INFO, "Using tty %s", tty_path);
	fd = open(tty_path, O_RDWR | O_NOCTTY | O_CLOEXEC);

	if (fd == -1) {
		wlr_log_errno(WLR_ERROR, "Cannot open tty");
		return false;
	}

	ioctl(fd, VT_ACTIVATE, tty);
	ioctl(fd, VT_WAITACTIVE, tty);

	int old_kbmode;
	if (ioctl(fd, KDGKBMODE, &old_kbmode)) {
		wlr_log_errno(WLR_ERROR, "Failed to read tty %d keyboard mode", tty);
		goto error;
	}

	if (ioctl(fd, KDSKBMODE, K_CODE)) {
		wlr_log_errno(WLR_ERROR,
			"Failed to set keyboard mode K_CODE on tty %d", tty);
		goto error;
	}

	if (ioctl(fd, KDSETMODE, KD_GRAPHICS)) {
		wlr_log_errno(WLR_ERROR, "Failed to set graphics mode on tty %d", tty);
		goto error;
	}

	struct vt_mode mode = {
		.mode = VT_PROCESS,
		.relsig = SIGUSR2,
		.acqsig = SIGUSR2,
		.frsig = SIGIO, // has to be set
	};

	if (ioctl(fd, VT_SETMODE, &mode) < 0) {
		wlr_log(WLR_ERROR, "Failed to take control of tty %d", tty);
		goto error;
	}

	struct wl_event_loop *loop = wl_display_get_event_loop(display);
	session->vt_source = wl_event_loop_add_signal(loop, SIGUSR2,
		vt_handler, session);
	if (!session->vt_source) {
		goto error;
	}

	session->base.vtnr = tty;
	session->tty_fd = fd;
	session->old_tty = old_tty;
	session->old_kbmode = old_kbmode;

	return true;

error:
	// In case we could not get the last active one, drop back to tty 1,
	// better than hanging in a useless blank console. Otherwise activate the
	// last active.
	ioctl(fd, VT_ACTIVATE, old_tty);
	close(fd);
	return false;
}

static struct wlr_session *direct_session_create(struct wl_display *disp) {
	struct direct_session *session = calloc(1, sizeof(*session));
	if (!session) {
		wlr_log_errno(WLR_ERROR, "Allocation failed");
		return NULL;
	}

	session_init(&session->base);
	session->sock = direct_ipc_init(&session->child);
	if (session->sock == -1) {
		goto error_session;
	}

	const char *seat = getenv("XDG_SEAT");
	if (!seat) {
		seat = "seat0";
	}

	if (strcmp(seat, "seat0") == 0) {
		if (!setup_tty(session, disp)) {
			goto error_ipc;
		}
	} else {
		session->base.vtnr = 0;
		session->tty_fd = -1;
	}

	wlr_log(WLR_INFO, "Successfully loaded direct session");

	snprintf(session->base.seat, sizeof(session->base.seat), "%s", seat);
	session->base.impl = &session_direct;
	session->base.active = true;
	return &session->base;

error_ipc:
	direct_ipc_finish(session->sock, session->child);
	close(session->sock);
error_session:
	free(session);
	return NULL;
}

const struct session_impl session_direct = {
	.create = direct_session_create,
	.destroy = direct_session_destroy,
	.open = direct_session_open,
	.close = direct_session_close,
	.change_vt = direct_change_vt,
};
