#define _POSIX_C_SOURCE 200809L
#ifdef __FreeBSD__
// for major/minor
#define __BSD_VISIBLE 1
#endif
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <pthread.h>
#include <wayland-server.h>
#include <wlr/backend/session/interface.h>
#include <wlr/config.h>
#include <wlr/util/log.h>
#include "util/signal.h"

#ifdef __linux__
#include <sys/eventfd.h>
#include <sys/sysmacros.h>
#elif defined(__FreeBSD__)
#include <sys/event.h>
#endif

#include <dbus/dbus.h>
#include <ConsoleKit/libconsolekit.h>

enum { DRM_MAJOR = 226 };

static int wlr_dbus_dispatch_watch(int fd, uint32_t mask, void *data) {
	DBusWatch *watch = data;
	uint32_t flags = 0;

	if (dbus_watch_get_enabled(watch)) {
		if (mask & WL_EVENT_READABLE)
			flags |= DBUS_WATCH_READABLE;
		if (mask & WL_EVENT_WRITABLE)
			flags |= DBUS_WATCH_WRITABLE;
		if (mask & WL_EVENT_HANGUP)
			flags |= DBUS_WATCH_HANGUP;
		if (mask & WL_EVENT_ERROR)
			flags |= DBUS_WATCH_ERROR;

		dbus_watch_handle(watch, flags);
	}

	return 0;
}

static unsigned wlr_dbus_add_watch(DBusWatch *watch, void *data) {
	struct wl_event_loop *loop = data;
	struct wl_event_source *src;
	int fd;
	uint32_t mask = 0, flags;

	if (dbus_watch_get_enabled(watch)) {
		flags = dbus_watch_get_flags(watch);
		if (flags & DBUS_WATCH_READABLE)
			mask |= WL_EVENT_READABLE;
		if (flags & DBUS_WATCH_WRITABLE)
			mask |= WL_EVENT_WRITABLE;
	}

	fd = dbus_watch_get_unix_fd(watch);
	src = wl_event_loop_add_fd(loop, fd, mask, wlr_dbus_dispatch_watch,
				 watch);
	if (!src)
		return FALSE;

	dbus_watch_set_data(watch, src, NULL);
	return TRUE;
}

static void wlr_dbus_remove_watch(DBusWatch *watch, void *data) {
	struct wl_event_source *src = dbus_watch_get_data(watch);

	if (!src)
		return;

	wl_event_source_remove(src);
}

static void wlr_dbus_toggle_watch(DBusWatch *watch, void *data) {
	struct wl_event_source *src = dbus_watch_get_data(watch);
	uint32_t mask = 0, flags;

	if (!src)
		return;

	if (dbus_watch_get_enabled(watch)) {
		flags = dbus_watch_get_flags(watch);
		if (flags & DBUS_WATCH_READABLE)
			mask |= WL_EVENT_READABLE;
		if (flags & DBUS_WATCH_WRITABLE)
			mask |= WL_EVENT_WRITABLE;
	}

	wl_event_source_fd_update(src, mask);
}

static int wlr_dbus_dispatch_timeout(void *data) {
	DBusTimeout *timeout = data;

	if (dbus_timeout_get_enabled(timeout))
		dbus_timeout_handle(timeout);

	return 0;
}

static int wlr_dbus_adjust_timeout(DBusTimeout *timeout,
							struct wl_event_source *s) {
	int64_t t = 0;

	if (dbus_timeout_get_enabled(timeout))
		t = dbus_timeout_get_interval(timeout);

	return wl_event_source_timer_update(s, t);
}

static unsigned wlr_dbus_add_timeout(DBusTimeout *timeout, void *data) {
	struct wl_event_loop *loop = data;
	int r;
	struct wl_event_source *s = wl_event_loop_add_timer(loop, wlr_dbus_dispatch_timeout,
			timeout);

	if (!s)
		return FALSE;

	r = wlr_dbus_adjust_timeout(timeout, s);
	if (r < 0) {
		wl_event_source_remove(s);
		return FALSE;
	}

	dbus_timeout_set_data(timeout, s, NULL);
	return TRUE;
}

static void wlr_dbus_remove_timeout(DBusTimeout *timeout, void *data) {
	struct wl_event_source *s = dbus_timeout_get_data(timeout);

	if (!s)
		return;

	wl_event_source_remove(s);
}

static void wlr_dbus_toggle_timeout(DBusTimeout *timeout, void *data) {
	struct wl_event_source *s = dbus_timeout_get_data(timeout);

	if (!s)
		return;

	wlr_dbus_adjust_timeout(timeout, s);
}

static int wlr_dbus_call_method(DBusConnection *bus, const char *destination,
		const char *path, const char *interface, const char *member,
		DBusError *ret_error, DBusMessage **reply, int arg1, ...) {

	DBusMessage *msg = NULL;
	bool b;
	va_list args;

	msg = dbus_message_new_method_call(destination, path, interface, member);
	if (!msg)
		return -ENOMEM;

	va_start(args, arg1);
	b = dbus_message_append_args_valist(msg, arg1, args);
	va_end(args);
	if (!b)
		return -ENOMEM;

	*reply = dbus_connection_send_with_reply_and_block(bus, msg, -1, NULL);

	return 0;
}

static bool is_drm_device(const char *path, struct stat *st) {
#ifdef __linux__
	return major(st.st_rdev) == DRM_MAJOR;
#else
	// On FreeBSD, /dev/dri/card0 is a symlink to /dev/drm/0
	return strstr(path, "dri/") != NULL || strstr(path, "drm/") != NULL;
#endif
}

const struct session_impl session_consolekit2;

struct consolekit2_session {
	struct wlr_session base;

	LibConsoleKit *ck;
	DBusConnection *bus;
	struct wl_event_source *event;

	char *path;

	// specifies whether a drm device was taken
	// if so, the session will be (de)activated with the drm fd,
	// otherwise with the dbus PropertiesChanged on "active" signal
	bool has_drm;

#ifndef __linux__
	// on FreeBSD, majors are not stable, so we have to keep track of the drm device
	dev_t drm_dev;
#endif
};

static struct consolekit2_session *consolekit2_session_from_session(
		struct wlr_session *base) {
	assert(base->impl == &session_consolekit2);
	return (struct consolekit2_session *)base;
}

static int consolekit2_take_device(struct wlr_session *base, const char *path) {
	struct consolekit2_session *session = consolekit2_session_from_session(base);

	int fd = -1;
	int ret;
	DBusMessage *msg = NULL;
	DBusError error = DBUS_ERROR_INIT;

	struct stat st;
	if (stat(path, &st) < 0) {
		wlr_log(WLR_ERROR, "Failed to stat '%s'", path);
		return -1;
	}

	if (is_drm_device(path, &st)) {
		session->has_drm = true;
#ifndef __linux__
		session->drm_dev = st.st_rdev;
#endif
	}

	uint32_t maj = major(st.st_rdev), min = minor(st.st_rdev);
	ret = wlr_dbus_call_method(session->bus, "org.freedesktop.ConsoleKit",
			session->path, "org.freedesktop.ConsoleKit.Session", "TakeDevice",
			&error, &msg,
			DBUS_TYPE_UINT32, &maj,
			DBUS_TYPE_UINT32, &min,
			DBUS_TYPE_INVALID);
	if (ret < 0) {
		wlr_log(WLR_ERROR, "Failed to take device '%s': %s", path,
				error.message);
		goto out;
	}

	int paused = 0;
	ret = dbus_message_get_args(msg, &error,
					DBUS_TYPE_UNIX_FD, &fd,
					DBUS_TYPE_BOOLEAN, &paused,
					DBUS_TYPE_INVALID);
	if (!ret) {
		wlr_log(WLR_ERROR, "Failed to parse D-Bus response for '%s': %s",
				path, error.message);
		goto out;
	}

	// The original fd seems to be closed when the message is freed
	// so we just clone it.
	fd = fcntl(fd, F_DUPFD_CLOEXEC, 0);
	if (fd < 0) {
		wlr_log(WLR_ERROR, "Failed to clone file descriptor for '%s': %s",
				path, strerror(errno));
	}

out:
	dbus_error_free(&error);
	if (msg != NULL)
		dbus_message_unref(msg);
	return fd;
}

static void consolekit2_release_device(struct wlr_session *base, int fd) {
	struct consolekit2_session *session = consolekit2_session_from_session(base);

	struct stat st;
	if (fstat(fd, &st) < 0) {
		wlr_log(WLR_ERROR, "Failed to stat device '%d': %s", fd,
				strerror(errno));
		return;
	}

	DBusMessage *msg = NULL;
	DBusError error = DBUS_ERROR_INIT;
	uint32_t maj = major(st.st_rdev), min = minor(st.st_rdev);
	int ret = wlr_dbus_call_method(session->bus, "org.freedesktop.ConsoleKit",
			session->path, "org.freedesktop.ConsoleKit.Session", "ReleaseDevice",
			&error, &msg,
			DBUS_TYPE_UINT32, &maj,
			DBUS_TYPE_UINT32, &min,
			DBUS_TYPE_INVALID);
	if (ret < 0) {
		wlr_log(WLR_ERROR, "Failed to release device '%d': %s", fd,
				error.message);
	}

	dbus_error_free(&error);
	if (msg != NULL)
		dbus_message_unref(msg);
	close(fd);
}

static bool consolekit2_change_vt(struct wlr_session *base, unsigned vt) {
	struct consolekit2_session *session = consolekit2_session_from_session(base);

	// Only seat0 has VTs associated with it
	if (strcmp(session->base.seat, "seat0") != 0) {
		return true;
	}

	int ret;
	DBusMessage *msg = NULL;
	DBusError error = DBUS_ERROR_INIT;

	ret = wlr_dbus_call_method(session->bus, "org.freedesktop.ConsoleKit",
			"/org/freedesktop/ConsoleKit/Seat1", "org.freedesktop.ConsoleKit.Seat", "SwitchTo",
			&error, &msg, DBUS_TYPE_UINT32, &vt, DBUS_TYPE_INVALID);
	if (ret < 0) {
		wlr_log(WLR_ERROR, "Failed to change to vt '%d'", vt);
	}

	dbus_error_free(&error);
	if (msg != NULL)
		dbus_message_unref(msg);
	return ret >= 0;
}

static bool session_activate(struct consolekit2_session *session) {
	int ret;
	DBusMessage *msg = NULL;
	DBusError error = DBUS_ERROR_INIT;

	ret = wlr_dbus_call_method(session->bus, "org.freedesktop.ConsoleKit",
			session->path, "org.freedesktop.ConsoleKit.Session", "Activate",
			&error, &msg, DBUS_TYPE_INVALID);
	if (ret < 0) {
		wlr_log(WLR_ERROR, "Failed to activate session: %s", error.message);
	}

	dbus_error_free(&error);
	if (msg != NULL)
		dbus_message_unref(msg);
	return ret >= 0;
}

static bool take_control(struct consolekit2_session *session) {
	int ret;
	DBusMessage *msg = NULL;
	DBusError error = DBUS_ERROR_INIT;

	bool force = false;
	ret = wlr_dbus_call_method(session->bus, "org.freedesktop.ConsoleKit",
			session->path, "org.freedesktop.ConsoleKit.Session", "TakeControl",
			&error, &msg, DBUS_TYPE_BOOLEAN, &force, DBUS_TYPE_INVALID);
	if (ret < 0) {
		wlr_log(WLR_ERROR, "Failed to take control of session: %s",
				error.message);
	}

	dbus_error_free(&error);
	if (msg != NULL)
		dbus_message_unref(msg);
	return ret >= 0;
}

static void release_control(struct consolekit2_session *session) {
	int ret;
	DBusMessage *msg = NULL;
	DBusError error = DBUS_ERROR_INIT;

	ret = wlr_dbus_call_method(session->bus, "org.freedesktop.ConsoleKit",
			session->path, "org.freedesktop.ConsoleKit.Session", "ReleaseControl",
			&error, &msg, DBUS_TYPE_INVALID);
	if (ret < 0) {
		wlr_log(WLR_ERROR, "Failed to release control of session: %s",
				error.message);
	}

	dbus_error_free(&error);
	if (msg != NULL)
		dbus_message_unref(msg);
}

static void consolekit2_session_destroy(struct wlr_session *base) {
	struct consolekit2_session *session = consolekit2_session_from_session(base);

	release_control(session);

	dbus_connection_close(session->bus);
	wl_event_source_remove(session->event);
	free(session->path);
	free(session);
}

static int session_removed(DBusMessage *msg, void *userdata) {
	wlr_log(WLR_INFO, "SessionRemoved signal received");
	return 0;
}

static struct wlr_device *find_device(struct wlr_session *session,
		dev_t devnum) {
	struct wlr_device *dev;

	wl_list_for_each(dev, &session->devices, link) {
		if (dev->dev == devnum) {
			return dev;
		}
	}

	wlr_log(WLR_ERROR, "Tried to use dev_t %lu not opened by session",
			(unsigned long)devnum);
	assert(0);
}

static int pause_device(DBusMessage *msg, void *userdata) {
	struct consolekit2_session *session = userdata;
	int ret;
	DBusError err = DBUS_ERROR_INIT;

	uint32_t major, minor;
	const char *type;
	ret = dbus_message_get_args(msg, &err,
			DBUS_TYPE_UINT32, &major,
			DBUS_TYPE_UINT32, &minor,
			DBUS_TYPE_STRING, &type,
			DBUS_TYPE_INVALID);
	if (ret < 0) {
		wlr_log(WLR_ERROR, "Failed to parse D-Bus response for PauseDevice: %s",
				err.message);
		goto error;
	}

#ifdef __linux__
	if (major == DRM_MAJOR) {
#else
	if (makedev(major, minor) == session->drm_dev) {
#endif
		assert(session->has_drm);
		session->base.active = false;
		wlr_signal_emit_safe(&session->base.session_signal, session);
	}

	if (strcmp(type, "pause") == 0) {
		ret = wlr_dbus_call_method(session->bus, "org.freedesktop.ConsoleKit",
				session->path, "org.freedesktop.ConsoleKit.Session", "PauseDeviceComplete",
				&err, &msg,
				DBUS_TYPE_UINT32, major,
				DBUS_TYPE_UINT32, minor,
				DBUS_TYPE_INVALID);
		if (ret < 0) {
			wlr_log(WLR_ERROR, "Failed to send PauseDeviceComplete signal: %s",
					err.message);
		}
	}

error:
	return 0;
}

static int resume_device(DBusMessage *msg, void *userdata) {
	struct consolekit2_session *session = userdata;
	int ret;
	DBusError err = DBUS_ERROR_INIT;

	uint32_t major, minor, fd;
	ret = dbus_message_get_args(msg, &err,
			DBUS_TYPE_UINT32, &major,
			DBUS_TYPE_UINT32, &minor,
			DBUS_TYPE_UNIX_FD, &fd,
			DBUS_TYPE_INVALID);
	if (ret < 0) {
		wlr_log(WLR_ERROR, "Failed to parse D-Bus response for ResumeDevice: %s",
				err.message);
		goto error;
	}

#ifdef __linux__
	if (major == DRM_MAJOR) {
#else
	if (makedev(major, minor) == session->drm_dev) {
#endif
		struct wlr_device *dev = find_device(&session->base, makedev(major, minor));
		dup2(fd, dev->fd);
		if (!session->base.active) {
			session->base.active = true;
			wlr_signal_emit_safe(&session->base.session_signal, session);
		}
	}

error:
	return 0;
}

static int properties_changed(DBusMessage *msg, void *userdata) {
	struct consolekit2_session *session = userdata;
	int ret = 0;

	// if we have a drm fd we don't depend on this..
	if (session->has_drm) {
		return 0;
	}

	DBusMessageIter iter, sub, entry, entry_val;

	if (!dbus_message_iter_init(msg, &iter) ||
			dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_STRING)
		goto error;

	// PropertiesChanged arg 1: interface
	const char *interface;
	dbus_message_iter_get_basic(&iter, &interface);
	if (strcmp(interface, "org.freedesktop.ConsoleKit.Session") != 0) {
		// not interesting for us; ignore
		wlr_log(WLR_DEBUG, "ignoring PropertiesChanged from %s", interface);
		return 0;
	}

	// PropertiesChanged arg 2: changed properties with values
	if (!dbus_message_iter_next(&iter) ||
			dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_ARRAY)
		goto error;

	dbus_message_iter_recurse(&iter, &sub);

	while (dbus_message_iter_get_arg_type(&sub) == DBUS_TYPE_DICT_ENTRY) {
		dbus_message_iter_recurse(&sub, &entry);

		if (dbus_message_iter_get_arg_type(&entry) != DBUS_TYPE_STRING)
			goto error;

		const char *name;
		dbus_message_iter_get_basic(&entry, &name);

		if (!dbus_message_iter_next(&entry))
			goto error;

		if (strcmp(name, "active") == 0) {
			if (dbus_message_iter_get_arg_type(&entry) != DBUS_TYPE_VARIANT)
				goto error;

			dbus_message_iter_recurse(&entry, &entry_val);

			if (dbus_message_iter_get_arg_type(&entry_val) != DBUS_TYPE_BOOLEAN)
				goto error;

			bool active = false;
			dbus_message_iter_get_basic(&entry_val, &active);
			if (session->base.active != active) {
				session->base.active = active;
				wlr_signal_emit_safe(&session->base.session_signal, session);
			}

			return 0;
		}

		dbus_message_iter_next(&sub);
	}

	if (!dbus_message_iter_next(&iter) ||
			dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_ARRAY)
		goto error;

	dbus_message_iter_recurse(&iter, &sub);

	while (dbus_message_iter_get_arg_type(&sub) == DBUS_TYPE_STRING) {
		const char *name;
		dbus_message_iter_get_basic(&sub, &name);

		if (strcmp(name, "active") == 0) {
			DBusError err = DBUS_ERROR_INIT;
			ret = wlr_dbus_call_method(session->bus, "org.freedesktop.ConsoleKit",
					session->path, "org.freedesktop.DBus.Properties", "Get",
					&err, &msg,
					DBUS_TYPE_STRING, "org.freedesktop.ConsoleKit.Session",
					DBUS_TYPE_STRING, "active",
					DBUS_TYPE_INVALID);
			if (ret < 0) {
				wlr_log(WLR_ERROR, "Failed to get 'active' property: %s",
					err.message);
				return 0;
			}

			if (!dbus_message_iter_init(msg, &iter) ||
					dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_VARIANT)
				goto error;

			dbus_message_iter_recurse(&iter, &entry_val);

			if (dbus_message_iter_get_arg_type(&entry_val) != DBUS_TYPE_BOOLEAN)
				goto error;

			bool active;
			dbus_message_iter_get_basic(&entry_val, &active);
			if (session->base.active != active) {
				session->base.active = active;
				wlr_signal_emit_safe(&session->base.session_signal, session);
			}
			return 0;
		}

		dbus_message_iter_next(&sub);
	}

	if (ret < 0) {
		goto error;
	}

	return 0;

error:
	wlr_log(WLR_ERROR, "Failed to parse D-Bus PropertiesChanged: %s",
			strerror(-ret));
	return 0;
}

static bool add_signal_matches(struct consolekit2_session *session) {
	DBusError err = DBUS_ERROR_INIT;

	char str[256];
	const char *fmt = "type='signal',"
		"sender='org.freedesktop.ConsoleKit',"
		"interface='org.freedesktop.%s',"
		"member='%s',"
		"path='%s'";

	snprintf(str, sizeof(str), fmt, "ConsoleKit.Manager", "SessionRemoved",
			"/org/freedesktop/ConsoleKit");
	dbus_bus_add_match(session->bus, str, &err);
	if (dbus_error_is_set(&err)) {
		wlr_log(WLR_ERROR, "Failed to add D-Bus match: %s", err.message);
		dbus_error_free(&err);
		return false;
	}

	snprintf(str, sizeof(str), fmt, "ConsoleKit.Session", "PauseDevice", session->path);
	dbus_bus_add_match(session->bus, str, &err);
	if (dbus_error_is_set(&err)) {
		wlr_log(WLR_ERROR, "Failed to add D-Bus match: %s", err.message);
		dbus_error_free(&err);
		return false;
	}

	snprintf(str, sizeof(str), fmt, "ConsoleKit.Session", "ResumeDevice", session->path);
	dbus_bus_add_match(session->bus, str, &err);
	if (dbus_error_is_set(&err)) {
		wlr_log(WLR_ERROR, "Failed to add D-Bus match: %s", err.message);
		dbus_error_free(&err);
		return false;
	}

	snprintf(str, sizeof(str), fmt, "DBus.Properties", "PropertiesChanged", session->path);
	dbus_bus_add_match(session->bus, str, &err);
	if (dbus_error_is_set(&err)) {
		wlr_log(WLR_ERROR, "Failed to add D-Bus match: %s", err.message);
		dbus_error_free(&err);
		return false;
	}

	return true;
}

static DBusHandlerResult
filter_dbus(DBusConnection *bus, DBusMessage *msg, void *data) {
	struct consolekit2_session *session = data;

	if (dbus_message_is_signal(msg, "org.freedesktop.ConsoleKit.Manager",
						"SessionRemoved")) {
		session_removed(msg, session);
	} else if (dbus_message_is_signal(msg, "org.freedesktop.DBus.Properties",
						"PropertiesChanged")) {
		properties_changed(msg, session);
	} else if (dbus_message_is_signal(msg, "org.freedesktop.ConsoleKit.Session",
						"PauseDevice")) {
		pause_device(msg, session);
	} else if (dbus_message_is_signal(msg, "org.freedesktop.ConsoleKit.Session",
						"ResumeDevice")) {
		resume_device(msg, session);
	}
	return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static int dbus_event(int fd, uint32_t mask, void *data) {
	DBusConnection *bus = data;
	int ret;

	do {
		ret = dbus_connection_dispatch(bus);
		if (ret == DBUS_DISPATCH_COMPLETE)
			ret = 0;
		else if (ret == DBUS_DISPATCH_DATA_REMAINS)
			ret = -EAGAIN;
		else if (ret == DBUS_DISPATCH_NEED_MEMORY)
			ret = -ENOMEM;
		else
			ret = -EIO;
	} while (ret == -EAGAIN);

	if (ret != 0)
		wlr_log(WLR_ERROR, "Failed to dispatch D-Bus events: %d\n", ret);

	return ret;
}

static struct wlr_session *consolekit2_session_create(struct wl_display *disp) {
	int ret;
	GError *gerr = NULL;
	DBusError berr = DBUS_ERROR_INIT;

	struct consolekit2_session *session = calloc(1, sizeof(*session));
	if (!session) {
		wlr_log(WLR_ERROR, "Allocation failed: %s", strerror(errno));
		return NULL;
	}

	// We have to mask SIGUSR1 in the D-Bus threads we spawn here,
	// otherwise Xwayland signals will quit the compositor
	sigset_t sigset;
	sigemptyset(&sigset);
	sigaddset(&sigset, SIGUSR1);
	pthread_sigmask(SIG_BLOCK, &sigset, NULL);

	session->ck = lib_consolekit_new();
	ret = lib_consolekit_pid_get_session(session->ck, getpid(), &session->path, &gerr);
	if (gerr != NULL) {
		wlr_log(WLR_ERROR, "Failed to get session path: %s", gerr->message);
		goto error;
	}

	char *seat = NULL;
	gerr = NULL;
	ret = lib_consolekit_session_get_seat(session->ck, session->path, &seat, &gerr);
	if (gerr != NULL) {
		wlr_log(WLR_ERROR, "Failed to get seat id: %s", gerr->message);
		goto error;
	}

	// ConsoleKit2 counts seats from 1 and returns the full D-Bus path.
	// Let's not expose that to the rest of the system.
	if (strcmp(seat, "/org/freedesktop/ConsoleKit/Seat1") == 0) {
		snprintf(session->base.seat, sizeof(session->base.seat), "seat0");
		gerr = NULL;
		ret = lib_consolekit_session_get_vt(session->ck, session->path, &session->base.vtnr, &gerr);
		if (gerr != NULL) {
			wlr_log(WLR_ERROR, "Session not running in virtual terminal");
			goto error;
		}
	} else {
		snprintf(session->base.seat, sizeof(session->base.seat), "%s", seat);
	}
	free(seat);

	dbus_connection_set_change_sigpipe(false);

	session->bus = dbus_bus_get_private(DBUS_BUS_SYSTEM, &berr);
	if (!session->bus) {
		wlr_log(WLR_ERROR, "Failed to open D-Bus connection: %s", berr.message);
		goto error;
	}

	dbus_connection_set_exit_on_disconnect(session->bus, FALSE);

	struct wl_event_loop *event_loop = wl_display_get_event_loop(disp);

	/* Idle events cannot reschedule themselves, therefore we use a dummy
	 * event-fd and mark it for post-dispatch. Hence, the dbus
	 * dispatcher is called after every dispatch-round.
	 * This is required as dbus doesn't allow dispatching events from
	 * within its own event sources. */
#ifdef __linux__
	int fd = eventfd(0, EFD_CLOEXEC);
#elif defined(__FreeBSD__)
	int fd = kqueue();
#endif
	if (fd < 0)
		goto error;

	session->event = wl_event_loop_add_fd(event_loop, fd, 0, dbus_event, session->bus);
	close(fd);

	if (!session->event)
		goto error;

	wl_event_source_check(session->event);

	if (!dbus_connection_set_watch_functions(session->bus,
							wlr_dbus_add_watch,
							wlr_dbus_remove_watch,
							wlr_dbus_toggle_watch,
							event_loop,
							NULL)) {
		wlr_log(WLR_ERROR, "Failed to configure D-Bus watch functions");
		goto error;
	}

	if (!dbus_connection_set_timeout_functions(session->bus,
							wlr_dbus_add_timeout,
							wlr_dbus_remove_timeout,
							wlr_dbus_toggle_timeout,
							event_loop,
							NULL)) {
		wlr_log(WLR_ERROR, "Failed to configure D-Bus timeout functions");
		goto error;
	}

	dbus_connection_ref(session->bus);

	if (!add_signal_matches(session)) {
		goto error_bus;
	}

	if (!dbus_connection_add_filter(session->bus, filter_dbus, session, NULL)) {
		wlr_log(WLR_ERROR, "Failed to add the D-Bus filter");
		goto error;
	}

	if (!take_control(session)) {
		goto error_bus;
	}

	if (!session_activate(session)) {
		goto error_bus;
	}

	wlr_log(WLR_INFO, "Successfully loaded consolekit2 session");

	// At this point, the D-Bus threads were spawned, so remove this mask to avoid touching
	// spawned client processes. Let the xwayland code do its own mask in its subprocess.
	sigfillset(&sigset);
	sigprocmask(SIG_UNBLOCK, &sigset, NULL);

	session->base.impl = &session_consolekit2;
	return &session->base;

error_bus:
	dbus_connection_close(session->bus);

error:
	free(session->path);
	return NULL;
}

const struct session_impl session_consolekit2 = {
	.create = consolekit2_session_create,
	.destroy = consolekit2_session_destroy,
	.open = consolekit2_take_device,
	.close = consolekit2_release_device,
	.change_vt = consolekit2_change_vt,
};
