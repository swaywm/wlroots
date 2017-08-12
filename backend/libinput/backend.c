#include <stdlib.h>
#include <assert.h>
#include <libinput.h>
#include <wlr/backend/session.h>
#include <wlr/backend/interface.h>
#include <wlr/util/log.h>
#include "backend/udev.h"
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
	if (libinput_dispatch(backend->libinput) != 0) {
		wlr_log(L_ERROR, "Failed to dispatch libinput");
		// TODO: some kind of abort?
		return 0;
	}
	struct libinput_event *event;
	while ((event = libinput_get_event(backend->libinput))) {
		wlr_libinput_event(backend, event);
		libinput_event_destroy(event);
	}
	return 0;
}

static void wlr_libinput_log(struct libinput *libinput,
		enum libinput_log_priority priority, const char *fmt, va_list args) {
	_wlr_vlog(L_ERROR, fmt, args);
}

static bool wlr_libinput_backend_init(struct wlr_backend *_backend) {
	struct wlr_libinput_backend *backend = (struct wlr_libinput_backend *)_backend;
	wlr_log(L_DEBUG, "Initializing libinput");
	backend->libinput = libinput_udev_create_context(&libinput_impl, backend,
			backend->udev->udev);
	if (!backend->libinput) {
		wlr_log(L_ERROR, "Failed to create libinput context");
		return false;
	}

	// TODO: Let user customize seat used
	if (libinput_udev_assign_seat(backend->libinput, "seat0") != 0) {
		wlr_log(L_ERROR, "Failed to assign libinput seat");
		return false;
	}

	// TODO: More sophisticated logging
	libinput_log_set_handler(backend->libinput, wlr_libinput_log);
	libinput_log_set_priority(backend->libinput, LIBINPUT_LOG_PRIORITY_ERROR);

	struct wl_event_loop *event_loop =
		wl_display_get_event_loop(backend->display);
	if (backend->input_event) {
		wl_event_source_remove(backend->input_event);
	}
	backend->input_event = wl_event_loop_add_fd(event_loop,
			libinput_get_fd(backend->libinput), WL_EVENT_READABLE,
			wlr_libinput_readable, backend);
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
	for (size_t i = 0; i < backend->devices->length; i++) {
		list_t *wlr_devices = backend->devices->items[i];
		for (size_t j = 0; j < wlr_devices->length; j++) {
			struct wlr_input_device *wlr_device = wlr_devices->items[j];
			wl_signal_emit(&backend->backend.events.input_remove, wlr_device);
			wlr_input_device_destroy(wlr_device);
		}
		list_free(wlr_devices);
	}
	list_free(backend->devices);
	libinput_unref(backend->libinput);
	free(backend);
}

static struct wlr_backend_impl backend_impl = {
	.init = wlr_libinput_backend_init,
	.destroy = wlr_libinput_backend_destroy
};

static void session_signal(struct wl_listener *listener, void *data) {
	struct wlr_libinput_backend *backend = wl_container_of(listener, backend, session_signal);
	struct wlr_session *session = data;

	if (!backend->libinput) {
		return;
	}

	if (session->active) {
		libinput_resume(backend->libinput);
	} else {
		libinput_suspend(backend->libinput);
	}
}

struct wlr_backend *wlr_libinput_backend_create(struct wl_display *display,
		struct wlr_session *session, struct wlr_udev *udev) {
	assert(display && session && udev);

	struct wlr_libinput_backend *backend = calloc(1, sizeof(struct wlr_libinput_backend));
	if (!backend) {
		wlr_log(L_ERROR, "Allocation failed: %s", strerror(errno));
		return NULL;
	}
	wlr_backend_create(&backend->backend, &backend_impl);

	if (!(backend->devices = list_create())) {
		wlr_log(L_ERROR, "Allocation failed: %s", strerror(errno));
		goto error_backend;
	}

	backend->session = session;
	backend->udev = udev;
	backend->display = display;

	backend->session_signal.notify = session_signal;
	wl_signal_add(&session->session_signal, &backend->session_signal);

	return &backend->backend;
error_backend:
	free(backend);
	return NULL;
}
