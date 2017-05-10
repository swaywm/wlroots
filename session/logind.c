#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <systemd/sd-bus.h>
#include <systemd/sd-login.h>
#include <unistd.h>
#include <sys/sysmacros.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <wayland-server.h>

#include "session/interface.h"
#include "common/log.h"

struct logind_session {
	struct wlr_session base;

	sd_bus *bus;

	char *id;
	char *path;
	char *seat;
};

static int logind_take_device(struct wlr_session *restrict base,
	const char *restrict path) {

	struct logind_session *session = wl_container_of(base, session, base);

	int ret;
	int fd = -1;
	sd_bus_message *msg = NULL;
	sd_bus_error error = SD_BUS_ERROR_NULL;

	struct stat st;
	if (stat(path, &st) < 0) {
		wlr_log(L_ERROR, "Failed to stat '%s'", path);
		return -1;
	}

	ret = sd_bus_call_method(session->bus, "org.freedesktop.login1",
		session->path, "org.freedesktop.login1.Session", "TakeDevice",
		&error, &msg, "uu", major(st.st_rdev), minor(st.st_rdev));
	if (ret < 0) {
		wlr_log(L_ERROR, "Failed to take device '%s': %s", path, error.message);
		goto error;
	}

	int paused = 0;
	ret = sd_bus_message_read(msg, "hb", &fd, &paused);
	if (ret < 0) {
		wlr_log(L_ERROR, "Failed to parse DBus response for '%s': %s",
			path, strerror(-ret));
		goto error;
	}

	// The original fd seem to be closed when the message is freed
	// so we just clone it.
	fd = fcntl(fd, F_DUPFD_CLOEXEC, 0);
	if (fd == -1) {
		wlr_log(L_ERROR, "Failed to clone file descriptor for '%s': %s",
			path, strerror(errno));
		goto error;
	}

error:
	sd_bus_error_free(&error);
	sd_bus_message_unref(msg);
	return fd;
}

static void logind_release_device(struct wlr_session *base, int fd) {
	struct logind_session *session = wl_container_of(base, session, base);

	int ret;
	sd_bus_message *msg = NULL;
	sd_bus_error error = SD_BUS_ERROR_NULL;

	struct stat st;
	if (fstat(fd, &st) < 0) {
		wlr_log(L_ERROR, "Failed to stat device '%d'", fd);
		return;
	}

	ret = sd_bus_call_method(session->bus, "org.freedesktop.login1",
		session->path, "org.freedesktop.login1.Session", "ReleaseDevice",
		&error, &msg, "uu", major(st.st_rdev), minor(st.st_rdev));
	if (ret < 0) {
		wlr_log(L_ERROR, "Failed to release device '%d'", fd);
	}

	sd_bus_error_free(&error);
	sd_bus_message_unref(msg);
}

static bool session_activate(struct logind_session *session) {
	int ret;
	sd_bus_message *msg = NULL;
	sd_bus_error error = SD_BUS_ERROR_NULL;

	ret = sd_bus_call_method(session->bus, "org.freedesktop.login1",
		session->path, "org.freedesktop.login1.Session", "Activate",
		&error, &msg, "");
	if (ret < 0) {
		wlr_log(L_ERROR, "Failed to activate session");
	}

	sd_bus_error_free(&error);
	sd_bus_message_unref(msg);
	return ret >= 0;
}

static bool take_control(struct logind_session *session) {
	int ret;
	sd_bus_message *msg = NULL;
	sd_bus_error error = SD_BUS_ERROR_NULL;

	ret = sd_bus_call_method(session->bus, "org.freedesktop.login1",
		session->path, "org.freedesktop.login1.Session", "TakeControl",
		&error, &msg, "b", false);
	if (ret < 0) {
		wlr_log(L_ERROR, "Failed to take control of session");
	}

	sd_bus_error_free(&error);
	sd_bus_message_unref(msg);
	return ret >= 0;
}

static void release_control(struct logind_session *session) {
	int ret;
	sd_bus_message *msg = NULL;
	sd_bus_error error = SD_BUS_ERROR_NULL;

	ret = sd_bus_call_method(session->bus, "org.freedesktop.login1",
		session->path, "org.freedesktop.login1.Session", "ReleaseControl",
		&error, &msg, "");
	if (ret < 0) {
		wlr_log(L_ERROR, "Failed to release control of session");
	}

	sd_bus_error_free(&error);
	sd_bus_message_unref(msg);
}

static void logind_session_finish(struct wlr_session *base) {
	struct logind_session *session = wl_container_of(base, session, base);

	release_control(session);

	sd_bus_unref(session->bus);
	free(session->id);
	free(session->path);
	free(session->seat);
	free(session);
}

static struct wlr_session *logind_session_start(void) {
	int ret;
	struct logind_session *session = calloc(1, sizeof(*session));
	if (!session) {
		wlr_log(L_ERROR, "Allocation failed: %s", strerror(errno));
		return NULL;
	}

	ret = sd_pid_get_session(getpid(), &session->id);
	if (ret < 0) {
		wlr_log(L_ERROR, "Failed to get session id: %s", strerror(-ret));
		goto error;
	}

	ret = sd_session_get_seat(session->id, &session->seat);
	if (ret < 0) {
		wlr_log(L_ERROR, "Failed to get seat id: %s", strerror(-ret));
		goto error;
	}

	const char *fmt = "/org/freedesktop/login1/session/%s";
	int len = snprintf(NULL, 0, fmt, session->id);

	session->path = malloc(len + 1);
	if (!session->path)
		goto error;

	sprintf(session->path, fmt, session->id);

	ret = sd_bus_default_system(&session->bus);
	if (ret < 0) {
		wlr_log(L_ERROR, "Failed to open DBus connection: %s", strerror(-ret));
		goto error;
	}

	if (!session_activate(session))
		goto error_bus;

	if (!take_control(session))
		goto error_bus;

	wlr_log(L_INFO, "Successfully loaded logind session");

	session->base.iface = session_logind_iface;
	return &session->base;

error_bus:
	sd_bus_unref(session->bus);

error:
	free(session->path);
	free(session->id);
	free(session->seat);
	return NULL;
}

const struct session_interface session_logind_iface = {
	.start = logind_session_start,
	.finish = logind_session_finish,
	.open = logind_take_device,
	.close = logind_release_device,
};
