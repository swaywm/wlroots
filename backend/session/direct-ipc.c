#define _POSIX_C_SOURCE 200809L
#ifdef __FreeBSD__
#define __BSD_VISIBLE 1
#define INPUT_MAJOR 0
#endif
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <wlr/config.h>
#include <wlr/util/log.h>
#include <xf86drm.h>
#ifdef __linux__
#include <sys/sysmacros.h>
#include <linux/major.h>
#endif
#include "backend/session/direct-ipc.h"

enum { DRM_MAJOR = 226 };

#ifdef WLR_HAS_LIBCAP
#include <sys/capability.h>

static bool have_permissions(void) {
	cap_t cap = cap_get_proc();
	cap_flag_value_t val;

	if (!cap || cap_get_flag(cap, CAP_SYS_ADMIN, CAP_PERMITTED, &val) || val != CAP_SET) {
		wlr_log(WLR_ERROR, "Do not have CAP_SYS_ADMIN; cannot become DRM master");
		cap_free(cap);
		return false;
	}

	cap_free(cap);
	return true;
}
#else
static bool have_permissions(void) {
#ifdef __linux__
	if (geteuid() != 0) {
		wlr_log(WLR_ERROR, "Do not have root privileges; cannot become DRM master");
		return false;
	}
#endif
	return true;
}
#endif

static void send_msg(int sock, int fd, void *buf, size_t buf_len) {
	char control[CMSG_SPACE(sizeof(fd))] = {0};
	struct iovec iovec = { .iov_base = buf, .iov_len = buf_len };
	struct msghdr msghdr = {0};

	if (buf) {
		msghdr.msg_iov = &iovec;
		msghdr.msg_iovlen = 1;
	}

	if (fd >= 0) {
		msghdr.msg_control = &control;
		msghdr.msg_controllen = sizeof(control);

		struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msghdr);
		*cmsg = (struct cmsghdr) {
			.cmsg_level = SOL_SOCKET,
			.cmsg_type = SCM_RIGHTS,
			.cmsg_len = CMSG_LEN(sizeof(fd)),
		};
		memcpy(CMSG_DATA(cmsg), &fd, sizeof(fd));
	}

	ssize_t ret;
	do {
		ret = sendmsg(sock, &msghdr, 0);
	} while (ret < 0 && errno == EINTR);
}

static ssize_t recv_msg(int sock, int *fd_out, void *buf, size_t buf_len) {
	char control[CMSG_SPACE(sizeof(*fd_out))] = {0};
	struct iovec iovec = { .iov_base = buf, .iov_len = buf_len };
	struct msghdr msghdr = {0};

	if (buf) {
		msghdr.msg_iov = &iovec;
		msghdr.msg_iovlen = 1;
	}

	if (fd_out) {
		msghdr.msg_control = &control;
		msghdr.msg_controllen = sizeof(control);
	}

	ssize_t ret;
	do {
		ret = recvmsg(sock, &msghdr, MSG_CMSG_CLOEXEC);
	} while (ret < 0 && errno == EINTR);

	if (fd_out) {
		struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msghdr);
		if (cmsg) {
			memcpy(fd_out, CMSG_DATA(cmsg), sizeof(*fd_out));
		} else {
			*fd_out = -1;
		}
	}

	return ret;
}

enum msg_type {
	MSG_OPEN,
	MSG_SETMASTER,
	MSG_DROPMASTER,
	MSG_END,
};

struct msg {
	enum msg_type type;
	char path[256];
};

static void communicate(int sock) {
	struct msg msg;
	int drm_fd = -1;
	bool running = true;

	while (running && recv_msg(sock, &drm_fd, &msg, sizeof(msg)) > 0) {
		switch (msg.type) {
		case MSG_OPEN:
			errno = 0;

			// These are the same flags that logind opens files with
			int fd = open(msg.path, O_RDWR|O_CLOEXEC|O_NOCTTY|O_NONBLOCK);
			int ret = errno;
			if (fd == -1) {
				goto error;
			}

			struct stat st;
			if (fstat(fd, &st) < 0) {
				ret = errno;
				goto error;
			}

			uint32_t maj = major(st.st_rdev);
			if (maj != INPUT_MAJOR && maj != DRM_MAJOR) {
				ret = ENOTSUP;
				goto error;
			}

			if (maj == DRM_MAJOR && drmSetMaster(fd)) {
				ret = errno;
			}
error:
			send_msg(sock, ret ? -1 : fd, &ret, sizeof(ret));
			if (fd >= 0) {
				close(fd);
			}

			break;

		case MSG_SETMASTER:
			drmSetMaster(drm_fd);
			close(drm_fd);
			send_msg(sock, -1, NULL, 0);
			break;

		case MSG_DROPMASTER:
			drmDropMaster(drm_fd);
			close(drm_fd);
			send_msg(sock, -1, NULL, 0);
			break;

		case MSG_END:
			running = false;
			send_msg(sock, -1, NULL, 0);
			break;
		}
	}

	close(sock);
}

int direct_ipc_open(int sock, const char *path) {
	struct msg msg = { .type = MSG_OPEN };
	snprintf(msg.path, sizeof(msg.path), "%s", path);

	send_msg(sock, -1, &msg, sizeof(msg));

	int fd, err;
	recv_msg(sock, &fd, &err, sizeof(err));

	return err ? -err : fd;
}

void direct_ipc_setmaster(int sock, int fd) {
	struct msg msg = { .type = MSG_SETMASTER };

	send_msg(sock, fd, &msg, sizeof(msg));
	recv_msg(sock, NULL, NULL, 0);
}

void direct_ipc_dropmaster(int sock, int fd) {
	struct msg msg = { .type = MSG_DROPMASTER };

	send_msg(sock, fd, &msg, sizeof(msg));
	recv_msg(sock, NULL, NULL, 0);
}

void direct_ipc_finish(int sock, pid_t pid) {
	struct msg msg = { .type = MSG_END };

	send_msg(sock, -1, &msg, sizeof(msg));
	recv_msg(sock, NULL, NULL, 0);

	waitpid(pid, NULL, 0);
}

int direct_ipc_init(pid_t *pid_out) {
	if (!have_permissions()) {
		return -1;
	}

	int sock[2];
	if (socketpair(AF_UNIX, SOCK_SEQPACKET, 0, sock) < 0) {
		wlr_log_errno(WLR_ERROR, "Failed to create socket pair");
		return -1;
	}

	pid_t pid = fork();
	if (pid < 0) {
		wlr_log_errno(WLR_ERROR, "Fork failed");
		close(sock[0]);
		close(sock[1]);
		return -1;
	} else if (pid == 0) {
		close(sock[0]);
		communicate(sock[1]);
		_Exit(0);
	}

	close(sock[1]);
	*pid_out = pid;
	return sock[0];
}
