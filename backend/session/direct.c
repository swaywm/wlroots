#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/kd.h>
#include <linux/major.h>
#include <linux/vt.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>
#include <wayland-server.h>
#include <wlr/backend/session/interface.h>
#include <wlr/util/log.h>
#include "backend/session/direct-ipc.h"
#include "util/signal.h"

enum { DRM_MAJOR = 226 };

const struct session_impl session_direct;

struct direct_session {
	struct wlr_session base;
	int tty_fd;
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

	struct stat st;
	if (fstat(fd, &st) < 0) {
		close(fd);
		return -errno;
	}

	if (major(st.st_rdev) == DRM_MAJOR) {
		direct_ipc_setmaster(session->sock, fd);
	}

	return fd;
}

static void direct_session_close(struct wlr_session *base, int fd) {
	struct direct_session *session = direct_session_from_session(base);

	struct stat st;
	if (fstat(fd, &st) < 0) {
		wlr_log_errno(WLR_ERROR, "Stat failed");
		close(fd);
		return;
	}

	if (major(st.st_rdev) == DRM_MAJOR) {
		direct_ipc_dropmaster(session->sock, fd);
	} else if (major(st.st_rdev) == INPUT_MAJOR) {
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

	if (session->base.active) {
		session->base.active = false;
		wlr_signal_emit_safe(&session->base.session_signal, session);

		struct wlr_device *dev;
		wl_list_for_each(dev, &session->base.devices, link) {
			if (major(dev->dev) == DRM_MAJOR) {
				direct_ipc_dropmaster(session->sock,
					dev->fd);
			}
		}

		ioctl(session->tty_fd, VT_RELDISP, 1);
	} else {
		ioctl(session->tty_fd, VT_RELDISP, VT_ACKACQ);

		struct wlr_device *dev;
		wl_list_for_each(dev, &session->base.devices, link) {
			if (major(dev->dev) == DRM_MAJOR) {
				direct_ipc_setmaster(session->sock,
					dev->fd);
			}
		}

		session->base.active = true;
		wlr_signal_emit_safe(&session->base.session_signal, session);
	}

	return 1;
}

static bool setup_tty(struct direct_session *session, struct wl_display *display) {
	int fd = open("/dev/tty", O_RDWR | O_CLOEXEC);
	if (fd == -1) {
		wlr_log_errno(WLR_ERROR, "Cannot open /dev/tty");
		return false;
	}

	struct vt_stat vt_stat;
	if (ioctl(fd, VT_GETSTATE, &vt_stat)) {
		wlr_log_errno(WLR_ERROR, "Could not get current tty number");
		goto error;
	}

	int tty = vt_stat.v_active;
	int ret, kd_mode, old_kbmode;

	ret = ioctl(fd, KDGETMODE, &kd_mode);
	if (ret) {
		wlr_log_errno(WLR_ERROR, "Failed to get tty mode");
		goto error;
	}

	if (kd_mode != KD_TEXT) {
		wlr_log(WLR_ERROR,
			"tty already in graphics mode; is another display server running?");
		goto error;
	}

	ioctl(fd, VT_ACTIVATE, tty);
	ioctl(fd, VT_WAITACTIVE, tty);

	if (ioctl(fd, KDGKBMODE, &old_kbmode)) {
		wlr_log_errno(WLR_ERROR, "Failed to read keyboard mode");
		goto error;
	}

	if (ioctl(fd, KDSKBMODE, K_OFF)) {
		wlr_log_errno(WLR_ERROR, "Failed to set keyboard mode");
		goto error;
	}

	if (ioctl(fd, KDSETMODE, KD_GRAPHICS)) {
		wlr_log_errno(WLR_ERROR, "Failed to set graphics mode on tty");
		goto error;
	}

	struct vt_mode mode = {
		.mode = VT_PROCESS,
		.relsig = SIGUSR2,
		.acqsig = SIGUSR2,
	};

	if (ioctl(fd, VT_SETMODE, &mode) < 0) {
		wlr_log(WLR_ERROR, "Failed to take control of tty");
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
	session->old_kbmode = old_kbmode;

	return true;

error:
	close(fd);
	return false;
}

static struct wlr_session *direct_session_create(struct wl_display *disp) {
	struct direct_session *session = calloc(1, sizeof(*session));
	if (!session) {
		wlr_log_errno(WLR_ERROR, "Allocation failed");
		return NULL;
	}

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

	snprintf(session->base.seat, sizeof(session->base.seat), "%s", seat);
	session->base.impl = &session_direct;

	wlr_log(WLR_INFO, "Successfully loaded direct session");
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
