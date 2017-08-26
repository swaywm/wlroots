#include <stdlib.h>
#include <assert.h>
#include <libinput.h>
#include <wlr/backend/session.h>
#include <wlr/backend/interface.h>
#include <wlr/util/log.h>
#include "backend/libinput.h"

static int wlr_libinput_open_restricted(const char *path,
		int flags, void *_backend) {
	struct wlr_libinput_backend *backend = _backend;
	return wlr_session_open_file(backend->session, path);
}

static void wlr_libinput_close_restricted(int fd, void *_backend) {
	struct wlr_libinput_backend *backend = _backend;
	wlr_session_close_file(backend->session, fd);
}

static const struct libinput_interface libinput_impl = {
	.open_restricted = wlr_libinput_open_restricted,
	.close_restricted = wlr_libinput_close_restricted
};

static int wlr_libinput_readable(int fd, uint32_t mask, void *_backend) {
	struct wlr_libinput_backend *backend = _backend;
	if (libinput_dispatch(backend->libinput_context) != 0) {
		wlr_log(L_ERROR, "Failed to dispatch libinput");
		// TODO: some kind of abort?
		return 0;
	}
	struct libinput_event *event;
	while ((event = libinput_get_event(backend->libinput_context))) {
		wlr_libinput_event(backend, event);
		libinput_event_destroy(event);
	}
	return 0;
}

static void wlr_libinput_log(struct libinput *libinput_context,
		enum libinput_log_priority priority, const char *fmt, va_list args) {
	_wlr_vlog(L_ERROR, fmt, args);
}

static bool wlr_libinput_backend_start(struct wlr_backend *_backend) {
	struct wlr_libinput_backend *backend = (struct wlr_libinput_backend *)_backend;
	wlr_log(L_DEBUG, "Initializing libinput");
	backend->libinput_context = libinput_udev_create_context(&libinput_impl, backend,
			backend->session->udev);
	if (!backend->libinput_context) {
		wlr_log(L_ERROR, "Failed to create libinput context");
		return false;
	}

	// TODO: Let user customize seat used
	if (libinput_udev_assign_seat(backend->libinput_context, "seat0") != 0) {
		wlr_log(L_ERROR, "Failed to assign libinput seat");
		return false;
	}

	// TODO: More sophisticated logging
	libinput_log_set_handler(backend->libinput_context, wlr_libinput_log);
	libinput_log_set_priority(backend->libinput_context, LIBINPUT_LOG_PRIORITY_ERROR);

	int libinput_fd = libinput_get_fd(backend->libinput_context);
	char *no_devs = getenv("WLR_LIBINPUT_NO_DEVICES");
	if (no_devs) {
		if (strcmp(no_devs, "1") != 0) {
			no_devs = NULL;
		}
	}
	if (!no_devs && backend->wlr_device_lists->length == 0) {
		wlr_libinput_readable(libinput_fd, WL_EVENT_READABLE, backend);
		if (backend->wlr_device_lists->length == 0) {
			wlr_log(L_ERROR, "libinput initialization failed, no input devices");
			wlr_log(L_ERROR, "Set WLR_LIBINPUT_NO_DEVICES=1 to suppress this check");
			return false;
		}
	}

	struct wl_event_loop *event_loop =
		wl_display_get_event_loop(backend->display);
	if (backend->input_event) {
		wl_event_source_remove(backend->input_event);
	}
	backend->input_event = wl_event_loop_add_fd(event_loop, libinput_fd,
			WL_EVENT_READABLE, wlr_libinput_readable, backend);
	if (!backend->input_event) {
		wlr_log(L_ERROR, "Failed to create input event on event loop");
		return false;
	}
	wlr_log(L_DEBUG, "libinput sucessfully initialized");
	return true;
}

static void wlr_libinput_backend_destroy(struct wlr_backend *_backend) {
	if (!_backend) {
		return;
	}
	struct wlr_libinput_backend *backend = (struct wlr_libinput_backend *)_backend;
	for (size_t i = 0; i < backend->wlr_device_lists->length; i++) {
		list_t *wlr_devices = backend->wlr_device_lists->items[i];
		for (size_t j = 0; j < wlr_devices->length; j++) {
			struct wlr_input_device *wlr_dev = wlr_devices->items[j];
			wl_signal_emit(&backend->backend.events.input_remove, wlr_dev);
			wlr_input_device_destroy(wlr_dev);
		}
		list_free(wlr_devices);
	}
	list_free(backend->wlr_device_lists);
	wl_event_source_remove(backend->input_event);
	libinput_unref(backend->libinput_context);
	free(backend);
}

static struct wlr_backend_impl backend_impl = {
	.start = wlr_libinput_backend_start,
	.destroy = wlr_libinput_backend_destroy
};

bool wlr_backend_is_libinput(struct wlr_backend *b) {
	return b->impl == &backend_impl;
}

static void session_signal(struct wl_listener *listener, void *data) {
	struct wlr_libinput_backend *backend = wl_container_of(listener, backend, session_signal);
	struct wlr_session *session = data;

	if (!backend->libinput_context) {
		return;
	}

	if (session->active) {
		libinput_resume(backend->libinput_context);
	} else {
		libinput_suspend(backend->libinput_context);
	}
}

struct wlr_backend *wlr_libinput_backend_create(struct wl_display *display,
		struct wlr_session *session) {
	assert(display && session);

	struct wlr_libinput_backend *backend = calloc(1, sizeof(struct wlr_libinput_backend));
	if (!backend) {
		wlr_log(L_ERROR, "Allocation failed: %s", strerror(errno));
		return NULL;
	}
	wlr_backend_init(&backend->backend, &backend_impl);

	if (!(backend->wlr_device_lists = list_create())) {
		wlr_log(L_ERROR, "Allocation failed: %s", strerror(errno));
		goto error_backend;
	}

	backend->session = session;
	backend->display = display;

	backend->session_signal.notify = session_signal;
	wl_signal_add(&session->session_signal, &backend->session_signal);

	return &backend->backend;
error_backend:
	free(backend);
	return NULL;
}

struct libinput_device *wlr_libinput_get_device_handle(struct wlr_input_device *_dev) {
	struct wlr_libinput_input_device *dev = (struct wlr_libinput_input_device *)_dev;
	return dev->handle;
}
