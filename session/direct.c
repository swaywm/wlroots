#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/capability.h>
#include <linux/kd.h>
#include <linux/major.h>
#include <linux/vt.h>
#include <wayland-server.h>
#include <xf86drm.h>
#include <wlr/session/interface.h>
#include <wlr/util/log.h>

#ifndef KDSKBMUTE
#define KDSKBMUTE	0x4B51
#endif

enum { DRM_MAJOR = 226 };

const struct session_impl session_direct;

struct direct_session {
	struct wlr_session base;
	int tty_fd;
	int drm_fd;
	int kb_mode;

	struct wl_event_source *vt_source;
};

static int direct_session_open(struct wlr_session *restrict base,
		const char *restrict path) {
	struct direct_session *session = wl_container_of(base, session, base);

	// These are the flags logind uses
	int fd = open(path, O_RDWR | O_CLOEXEC | O_NOCTTY | O_NONBLOCK);
	if (fd == -1) {
		wlr_log_errno(L_ERROR, "Cannot open %s", path);
		return -errno;
	}

	struct stat st;
	if (fstat(fd, &st) == 0 && major(st.st_rdev) == DRM_MAJOR) {
		if (drmSetMaster(fd)) {
			// Save errno, in case close() clobbers it
			int e = errno;
			wlr_log(L_ERROR, "Cannot become DRM master: %s%s", strerror(e),
				e == EINVAL ? "; is another display server running?" : "");
			close(fd);
			return -e;
		}

		session->drm_fd = fd;
	}

	return fd;
}

static void direct_session_close(struct wlr_session *base, int fd) {
	struct direct_session *session = wl_container_of(base, session, base);

	if (fd == session->drm_fd) {
		drmDropMaster(fd);
		session->drm_fd = -1;
	}

	close(fd);
}

static bool direct_change_vt(struct wlr_session *base, int vt) {
	struct direct_session *session = wl_container_of(base, session, base);
	return ioctl(session->tty_fd, VT_ACTIVATE, vt) == 0;
}

static void direct_session_finish(struct wlr_session *base) {
	struct direct_session *session = wl_container_of(base, session, base);
	struct vt_mode mode = {
		.mode = VT_AUTO,
	};

	if (ioctl(session->tty_fd, KDSKBMUTE, 0)) {
		ioctl(session->tty_fd, KDSKBMODE, session->kb_mode);
	}
	ioctl(session->tty_fd, KDSETMODE, KD_TEXT);
	ioctl(session->tty_fd, VT_SETMODE, &mode);

	wl_event_source_remove(session->vt_source);
	close(session->tty_fd);
	free(session);
}

static int vt_handler(int signo, void *data) {
	struct direct_session *session = data;

	if (session->base.active) {
		session->base.active = false;
		wl_signal_emit(&session->base.session_signal, session);
		drmDropMaster(session->drm_fd);
		ioctl(session->tty_fd, VT_RELDISP, 1);
	} else {
		ioctl(session->tty_fd, VT_RELDISP, VT_ACKACQ);
		drmSetMaster(session->drm_fd);
		session->base.active = true;
		wl_signal_emit(&session->base.session_signal, session);
	}

	return 1;
}

static bool setup_tty(struct direct_session *session, struct wl_display *display) {
	// TODO: Change this to accept any TTY, instead of just the current one
	session->tty_fd = dup(STDIN_FILENO);

	struct stat st;
	if (fstat(session->tty_fd, &st) == -1 || major(st.st_rdev) != TTY_MAJOR ||
			minor(st.st_rdev) == 0) {
		wlr_log(L_ERROR, "Not running from a virtual terminal");
		goto error;
	}

	int ret;

	int kd_mode;
	ret = ioctl(session->tty_fd, KDGETMODE, &kd_mode);
	if (ret) {
		wlr_log_errno(L_ERROR, "Failed to get tty mode");
		goto error;
	}

	if (kd_mode != KD_TEXT) {
		wlr_log(L_ERROR,
			"tty already in graphics mode; is another display server running?");
		goto error;
	}

	ioctl(session->tty_fd, VT_ACTIVATE, minor(st.st_rdev));
	ioctl(session->tty_fd, VT_WAITACTIVE, minor(st.st_rdev));

	if (ioctl(session->tty_fd, KDGKBMODE, &session->kb_mode)) {
		wlr_log_errno(L_ERROR, "Failed to read keyboard mode");
		goto error;
	}

	if (ioctl(session->tty_fd, KDSKBMUTE, 1) &&
			ioctl(session->tty_fd, KDSKBMODE, K_OFF)) {
		wlr_log_errno(L_ERROR, "Failed to set keyboard mode");
		goto error;
	}

	if (ioctl(session->tty_fd, KDSETMODE, KD_GRAPHICS)) {
		wlr_log_errno(L_ERROR, "Failed to set graphics mode on tty");
		goto error;
	}

	struct vt_mode mode = {
		.mode = VT_PROCESS,
		.relsig = SIGUSR1,
		.acqsig = SIGUSR1,
	};

	if (ioctl(session->tty_fd, VT_SETMODE, &mode) < 0) {
		wlr_log(L_ERROR, "Failed to take control of tty");
		goto error;
	}

	struct wl_event_loop *loop = wl_display_get_event_loop(display);
	session->vt_source = wl_event_loop_add_signal(loop, SIGUSR1,
		vt_handler, session);
	if (!session->vt_source) {
		goto error;
	}

	return true;

error:
	close(session->tty_fd);
	return false;
}

static struct wlr_session *direct_session_start(struct wl_display *disp) {
	cap_t cap = cap_get_proc();
	cap_flag_value_t val;

	if (!cap || cap_get_flag(cap, CAP_SYS_ADMIN, CAP_PERMITTED, &val) || val != CAP_SET) {
		wlr_log(L_ERROR, "Do not have CAP_SYS_ADMIN; cannot become DRM master");
		cap_free(cap);
		return NULL;
	}

	cap_free(cap);

	struct direct_session *session = calloc(1, sizeof(*session));
	if (!session) {
		wlr_log(L_ERROR, "Allocation failed: %s", strerror(errno));
		return NULL;
	}

	if (!setup_tty(session, disp)) {
		goto error_session;
	}

	wlr_log(L_INFO, "Successfully loaded direct session");

	session->base.impl = &session_direct;
	session->base.active = true;
	wl_signal_init(&session->base.session_signal);
	return &session->base;

error_session:
	free(session);
	return NULL;
}

const struct session_impl session_direct = {
	.start = direct_session_start,
	.finish = direct_session_finish,
	.open = direct_session_open,
	.close = direct_session_close,
	.change_vt = direct_change_vt,
};
