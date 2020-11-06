#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wlr/backend/session/interface.h>
#include <wlr/config.h>
#include <wlr/util/log.h>
#include "backend/session/session.h"
#include "util/signal.h"

#include <libseat.h>

const struct session_impl session_libseat;

struct libseat_device {
	struct wl_list link;
	int fd;
	int device_id;
};

struct libseat_session {
	struct wlr_session base;

	struct libseat *seat;
	struct wl_event_source *event;
	struct wl_list devices;
};

static void handle_enable_seat(struct libseat *seat, void *data) {
	struct libseat_session *session = data;
	session->base.active = true;
	wlr_signal_emit_safe(&session->base.events.active, NULL);
}

static void handle_disable_seat(struct libseat *seat, void *data) {
	struct libseat_session *session = data;
	session->base.active = false;
	wlr_signal_emit_safe(&session->base.events.active, NULL);
	libseat_disable_seat(session->seat);
}

static int libseat_event(int fd, uint32_t mask, void *data) {
	struct libseat *seat = data;
	libseat_dispatch(seat, 0);
	return 1;
}

static struct libseat_seat_listener seat_listener = {
	.enable_seat = handle_enable_seat,
	.disable_seat = handle_disable_seat,
};

static struct libseat_session *libseat_session_from_session(
		struct wlr_session *base) {
	assert(base->impl == &session_libseat);
	return (struct libseat_session *)base;
}

static enum wlr_log_importance libseat_log_level_to_wlr(
		enum libseat_log_level level) {
	switch (level) {
	case LIBSEAT_LOG_LEVEL_ERROR:
		return WLR_ERROR;
	case LIBSEAT_LOG_LEVEL_INFO:
		return WLR_INFO;
	default:
		return WLR_DEBUG;
	}
}

static void log_libseat(enum libseat_log_level level,
		const char *fmt, va_list args) {
	enum wlr_log_importance importance = libseat_log_level_to_wlr(level);

	static char wlr_fmt[1024];
	snprintf(wlr_fmt, sizeof(wlr_fmt), "[libseat] %s", fmt);

	_wlr_vlog(importance, wlr_fmt, args);
}

static struct wlr_session *libseat_session_create(struct wl_display *disp) {
	struct libseat_session *session = calloc(1, sizeof(*session));
	if (!session) {
		wlr_log_errno(WLR_ERROR, "Allocation failed");
		return NULL;
	}

	session_init(&session->base);
	wl_list_init(&session->devices);

	libseat_set_log_handler(log_libseat);
	libseat_set_log_level(LIBSEAT_LOG_LEVEL_ERROR);

	session->seat = libseat_open_seat(&seat_listener, session);
	if (session->seat == NULL) {
		wlr_log_errno(WLR_ERROR, "Unable to create seat");
		goto error;
	}

	const char *seat_name = libseat_seat_name(session->seat);
	if (seat_name == NULL) {
		wlr_log_errno(WLR_ERROR, "Unable to get seat info");
		goto error;
	}
	snprintf(session->base.seat, sizeof(session->base.seat), "%s", seat_name);

	struct wl_event_loop *event_loop = wl_display_get_event_loop(disp);
	session->event = wl_event_loop_add_fd(event_loop, libseat_get_fd(session->seat),
		WL_EVENT_READABLE, libseat_event, session->seat);
	if (session->event == NULL) {
		wlr_log(WLR_ERROR, "Failed to create libseat event source");
		goto error;
	}

	// We may have received enable_seat immediately after the open_seat result,
	// so, dispatch once without timeout to speed up activation.
	if (libseat_dispatch(session->seat, 0) == -1) {
		wlr_log_errno(WLR_ERROR, "libseat dispatch failed");
		goto error;
	}

	wlr_log(WLR_INFO, "Successfully loaded libseat session");
	session->base.impl = &session_libseat;
	return &session->base;

error:
	if (session->seat != NULL) {
		libseat_close_seat(session->seat);
	}
	if (session->event != NULL) {
		wl_event_source_remove(session->event);
	}
	free(session);
	return NULL;
}

static void libseat_session_destroy(struct wlr_session *base) {
	struct libseat_session *session = libseat_session_from_session(base);

	libseat_close_seat(session->seat);
	wl_event_source_remove(session->event);
	free(session);
}

static struct libseat_device *find_device_by_fd(struct libseat_session *session, int fd) {
	struct libseat_device *dev;
	wl_list_for_each(dev, &session->devices, link) {
		if (dev->fd == fd) {
			return dev;
		}
	}
	return NULL;
}

static int libseat_session_open_device(struct wlr_session *base, const char *path) {
	struct libseat_session *session = libseat_session_from_session(base);

	int fd;
	int device_id = libseat_open_device(session->seat, path, &fd);
	if (device_id == -1) {
		wlr_log_errno(WLR_ERROR, "Failed to open device '%s'", path);
		return -1;
	}

	struct libseat_device *dev = calloc(1, sizeof(struct libseat_device));
	if (dev == NULL) {
		wlr_log_errno(WLR_ERROR, "Allocation failed");
		libseat_close_device(session->seat, device_id);
		return -1;
	}

	dev->fd = fd;
	dev->device_id = device_id;
	wl_list_insert(&session->devices, &dev->link);

	return fd;
}

static void libseat_session_close_device(struct wlr_session *base, int fd) {
	struct libseat_session *session = libseat_session_from_session(base);

	struct libseat_device *dev = find_device_by_fd(session, fd);
	if (dev == NULL) {
		wlr_log(WLR_ERROR, "No device with fd %d found", fd);
		close(fd);
		return;
	}

	if (libseat_close_device(session->seat, dev->device_id) == -1) {
		wlr_log_errno(WLR_ERROR, "Failed to close device %d", dev->device_id);
	}

	wl_list_remove(&dev->link);
	free(dev);
	close(fd);
}

static bool libseat_change_vt(struct wlr_session *base, unsigned vt) {
	struct libseat_session *session = libseat_session_from_session(base);
	return libseat_switch_session(session->seat, vt);
}

const struct session_impl session_libseat = {
	.create = libseat_session_create,
	.destroy = libseat_session_destroy,
	.open = libseat_session_open_device,
	.close = libseat_session_close_device,
	.change_vt = libseat_change_vt,
};
