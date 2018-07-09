#define _XOPEN_SOURCE 700
#ifdef __FreeBSD__
// for SOCK_CLOEXEC
#define __BSD_VISIBLE 1
#endif
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>
#include <wlr/util/log.h>
#include "sockets.h"

static const char *lock_fmt = "/tmp/.X%d-lock";
static const char *socket_dir = "/tmp/.X11-unix";
static const char *socket_fmt = "/tmp/.X11-unix/X%d";
#ifndef __linux__
static const char *socket_fmt2 = "/tmp/.X11-unix/X%d_";
#endif

static int open_socket(struct sockaddr_un *addr, size_t path_size) {
	int fd, rc;
	socklen_t size = offsetof(struct sockaddr_un, sun_path) + path_size + 1;

	fd = socket(PF_LOCAL, SOCK_STREAM | SOCK_CLOEXEC, 0);
	if (fd < 0) {
		wlr_log_errno(WLR_DEBUG, "Failed to create socket %c%s",
			addr->sun_path[0] ? addr->sun_path[0] : '@',
			addr->sun_path + 1);
		return -1;
	}

	if (addr->sun_path[0]) {
		unlink(addr->sun_path);
	}
	if (bind(fd, (struct sockaddr*)addr, size) < 0) {
		rc = errno;
		wlr_log_errno(WLR_DEBUG, "Failed to bind socket %c%s",
			addr->sun_path[0] ? addr->sun_path[0] : '@',
			addr->sun_path + 1);
		goto cleanup;
	}
	if (listen(fd, 1) < 0) {
		rc = errno;
		wlr_log_errno(WLR_DEBUG, "Failed to listen to socket %c%s",
			addr->sun_path[0] ? addr->sun_path[0] : '@',
			addr->sun_path + 1);
		goto cleanup;
	}

	return fd;

cleanup:
	close(fd);
	if (addr->sun_path[0]) {
		unlink(addr->sun_path);
	}
	errno = rc;
	return -1;
}

static bool open_sockets(int socks[2], int display) {
	struct sockaddr_un addr = { .sun_family = AF_LOCAL };
	size_t path_size;

	mkdir(socket_dir, 0777);

#ifdef __linux__
	addr.sun_path[0] = 0;
	path_size = snprintf(addr.sun_path + 1, sizeof(addr.sun_path) - 1, socket_fmt, display);
#else
	path_size = snprintf(addr.sun_path, sizeof(addr.sun_path), socket_fmt2, display);
#endif
	socks[0] = open_socket(&addr, path_size);
	if (socks[0] < 0) {
		return false;
	}

	path_size = snprintf(addr.sun_path, sizeof(addr.sun_path), socket_fmt, display);
	socks[1] = open_socket(&addr, path_size);
	if (socks[1] < 0) {
		close(socks[0]);
		socks[0] = -1;
		return false;
	}

	return true;
}

void unlink_display_sockets(int display) {
	char sun_path[64];

	snprintf(sun_path, sizeof(sun_path), socket_fmt, display);
	unlink(sun_path);

#ifndef __linux__
	snprintf(sun_path, sizeof(sun_path), socket_fmt2, display);
	unlink(sun_path);
#endif

	snprintf(sun_path, sizeof(sun_path), lock_fmt, display);
	unlink(sun_path);
}

int open_display_sockets(int socks[2]) {
	int lock_fd, display;
	char lock_name[64];

	for (display = 0; display <= 32; display++) {
		snprintf(lock_name, sizeof(lock_name), lock_fmt, display);
		if ((lock_fd = open(lock_name, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0444)) >= 0) {
			if (!open_sockets(socks, display)) {
				unlink(lock_name);
				close(lock_fd);
				continue;
			}
			char pid[12];
			snprintf(pid, sizeof(pid), "%10d", getpid());
			if (write(lock_fd, pid, sizeof(pid) - 1) != sizeof(pid) - 1) {
				unlink(lock_name);
				close(lock_fd);
				continue;
			}
			close(lock_fd);
			break;
		}

		if ((lock_fd = open(lock_name, O_RDONLY | O_CLOEXEC)) < 0) {
			continue;
		}

		char pid[12] = { 0 }, *end_pid;
		ssize_t bytes = read(lock_fd, pid, sizeof(pid) - 1);
		close(lock_fd);

		if (bytes != sizeof(pid) - 1) {
			continue;
		}
		long int read_pid;
		read_pid = strtol(pid, &end_pid, 10);
		if (read_pid < 0 || read_pid > INT32_MAX || end_pid != pid + sizeof(pid) - 2) {
			continue;
		}
		errno = 0;
		if (kill((pid_t)read_pid, 0) != 0 && errno == ESRCH) {
			if (unlink(lock_name) != 0) {
				continue;
			}
			// retry
			display--;
			continue;
		}
	}

	if (display > 32) {
		wlr_log(WLR_ERROR, "No display available in the first 33");
		return -1;
	}

	return display;
}
