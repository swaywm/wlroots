#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <systemd/sd-bus.h>
#include <systemd/sd-login.h>
#include <unistd.h>
#include <sys/sysmacros.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "backend/drm/backend.h"
#include "backend/drm/session.h"

int wlr_session_take_device(struct wlr_session *restrict session,
		const char *restrict path,
		bool *restrict paused_out)
{
	int ret;
	int fd = -1;
	sd_bus_message *msg = NULL;
	sd_bus_error error = SD_BUS_ERROR_NULL;

	struct stat st;
	if (stat(path, &st) < 0) {
		fprintf(stderr, "Failed to stat '%s'\n", path);
		return -1;
	}

	ret = sd_bus_call_method(session->bus,
				 "org.freedesktop.login1",
				 session->path,
				 "org.freedesktop.login1.Session",
				 "TakeDevice",
				 &error, &msg,
				 "uu", major(st.st_rdev), minor(st.st_rdev));
	if (ret < 0) {
		fprintf(stderr, "%s\n", error.message);
		goto error;
	}

	int paused = 0;
	ret = sd_bus_message_read(msg, "hb", &fd, &paused);
	if (ret < 0) {
		fprintf(stderr, "%s\n", strerror(-ret));
		goto error;
	}

	// The original fd seem to be closed when the message is freed
	// so we just clone it.
	fd = fcntl(fd, F_DUPFD_CLOEXEC, 0);

	if (paused_out)
		*paused_out = paused;

error:
	sd_bus_error_free(&error);
	sd_bus_message_unref(msg);
	return fd;
}

void wlr_session_release_device(struct wlr_session *session, int fd)
{
	int ret;
	sd_bus_message *msg = NULL;
	sd_bus_error error = SD_BUS_ERROR_NULL;

	struct stat st;
	if (fstat(fd, &st) < 0) {
		fprintf(stderr, "Could not stat fd %d\n", fd);
		return;
	}

	ret = sd_bus_call_method(session->bus,
				 "org.freedesktop.login1",
				 session->path,
				 "org.freedesktop.login1.Session",
				 "ReleaseDevice",
				 &error, &msg,
				 "uu", major(st.st_rdev), minor(st.st_rdev));
	if (ret < 0) {
		/* Log something */;
	}

	sd_bus_error_free(&error);
	sd_bus_message_unref(msg);
}

static bool session_activate(struct wlr_session *session)
{
	int ret;
	sd_bus_message *msg = NULL;
	sd_bus_error error = SD_BUS_ERROR_NULL;

	ret = sd_bus_call_method(session->bus,
				 "org.freedesktop.login1",
				 session->path,
				 "org.freedesktop.login1.Session",
				 "Activate",
				 &error, &msg,
				 "");
	if (ret < 0) {
		fprintf(stderr, "%s\n", error.message);
	}

	sd_bus_error_free(&error);
	sd_bus_message_unref(msg);
	return ret >= 0;
}

static bool take_control(struct wlr_session *session)
{
	int ret;
	sd_bus_message *msg = NULL;
	sd_bus_error error = SD_BUS_ERROR_NULL;

	ret = sd_bus_call_method(session->bus,
				 "org.freedesktop.login1",
				 session->path,
				 "org.freedesktop.login1.Session",
				 "TakeControl",
				 &error, &msg,
				 "b", false);
	if (ret < 0) {
		/* Log something */;
	}

	sd_bus_error_free(&error);
	sd_bus_message_unref(msg);
	return ret >= 0;
}

static void release_control(struct wlr_session *session)
{
	int ret;
	sd_bus_message *msg = NULL;
	sd_bus_error error = SD_BUS_ERROR_NULL;

	ret = sd_bus_call_method(session->bus,
				 "org.freedesktop.login1",
				 session->path,
				 "org.freedesktop.login1.Session",
				 "ReleaseControl",
				 &error, &msg,
				 "");
	if (ret < 0) {
		/* Log something */;
	}

	sd_bus_error_free(&error);
	sd_bus_message_unref(msg);
}

void wlr_session_end(struct wlr_session *session)
{
	release_control(session);

	sd_bus_unref(session->bus);
	free(session->id);
	free(session->path);
	free(session->seat);
}

bool wlr_session_start(struct wlr_session *session)
{
	int ret;

	ret = sd_pid_get_session(getpid(), &session->id);
	if (ret < 0) {
		fprintf(stderr, "Could not get session\n");
		goto error;
	}

	ret = sd_session_get_seat(session->id, &session->seat);
	if (ret < 0) {
		fprintf(stderr, "Could not get seat\n");
		goto error;
	}

	// This could be done using asprintf, but I don't want to define _GNU_SOURCE

	const char *fmt = "/org/freedesktop/login1/session/%s";
	int len = snprintf(NULL, 0, fmt, session->id);

	session->path = malloc(len + 1);
	if (!session->path)
		goto error;

	sprintf(session->path, fmt, session->id);

	ret = sd_bus_open_system(&session->bus);
	if (ret < 0) {
		fprintf(stderr, "Could not open bus\n");
		goto error;
	}

	if (!session_activate(session)) {
		fprintf(stderr, "Could not activate session\n");
		goto error_bus;
	}

	if (!take_control(session)) {
		fprintf(stderr, "Could not take control of session\n");
		goto error_bus;
	}

	return true;

error_bus:
	sd_bus_unref(session->bus);

error:
	free(session->path);
	free(session->id);
	free(session->seat);
	return false;
}
