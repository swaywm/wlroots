#include <stdlib.h>
#include <assert.h>
#include <libinput.h>
#include <wlr/session.h>
#include <wlr/backend/interface.h>
#include <wlr/util/log.h>
#include "backend/udev.h"
#include "backend/libinput.h"

static int wlr_libinput_open_restricted(const char *path,
		int flags, void *_state) {
	struct wlr_backend_state *state = _state;
	return wlr_session_open_file(state->session, path);
}

static void wlr_libinput_close_restricted(int fd, void *_state) {
	struct wlr_backend_state *state = _state;
	wlr_session_close_file(state->session, fd);
}

static const struct libinput_interface libinput_impl = {
	.open_restricted = wlr_libinput_open_restricted,
	.close_restricted = wlr_libinput_close_restricted
};

static int wlr_libinput_readable(int fd, uint32_t mask, void *_state) {
	struct wlr_backend_state *state = _state;
	if (libinput_dispatch(state->libinput) != 0) {
		wlr_log(L_ERROR, "Failed to dispatch libinput");
		// TODO: some kind of abort?
		return 0;
	}
	struct libinput_event *event;
	while ((event = libinput_get_event(state->libinput))) {
		wlr_libinput_event(state, event);
	}
	return 0;
}

static void wlr_libinput_log(struct libinput *libinput,
		enum libinput_log_priority priority, const char *fmt, va_list args) {
	_wlr_vlog(L_ERROR, fmt, args);
}

static bool wlr_libinput_backend_init(struct wlr_backend_state *state) {
	wlr_log(L_DEBUG, "Initializing libinput");
	state->libinput = libinput_udev_create_context(&libinput_impl, state,
			state->udev->udev);
	if (!state->libinput) {
		wlr_log(L_ERROR, "Failed to create libinput context");
		return false;
	}

	// TODO: Let user customize seat used
	if (libinput_udev_assign_seat(state->libinput, "seat0") != 0) {
		wlr_log(L_ERROR, "Failed to assign libinput seat");
		return false;
	}

	// TODO: More sophisticated logging
	libinput_log_set_handler(state->libinput, wlr_libinput_log);
	libinput_log_set_priority(state->libinput, LIBINPUT_LOG_PRIORITY_ERROR);

	struct wl_event_loop *event_loop =
		wl_display_get_event_loop(state->display);
	if (state->input_event) {
		wl_event_source_remove(state->input_event);
	}
	state->input_event = wl_event_loop_add_fd(event_loop,
			libinput_get_fd(state->libinput), WL_EVENT_READABLE,
			wlr_libinput_readable, state);
	if (!state->input_event) {
		wlr_log(L_ERROR, "Failed to create input event on event loop");
		return false;
	}
	wlr_log(L_DEBUG, "libinput sucessfully initialized");
	return true;
}

static void wlr_libinput_backend_destroy(struct wlr_backend_state *state) {
	// TODO
}

static struct wlr_backend_impl backend_impl = {
	.init = wlr_libinput_backend_init,
	.destroy = wlr_libinput_backend_destroy
};

static void session_signal(struct wl_listener *listener, void *data) {
	struct wlr_backend_state *backend = wl_container_of(listener, backend, session_signal);
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

	struct wlr_backend_state *state = calloc(1, sizeof(struct wlr_backend_state));
	if (!state) {
		wlr_log(L_ERROR, "Allocation failed: %s", strerror(errno));
		return NULL;
	}

	struct wlr_backend *backend = wlr_backend_create(&backend_impl, state);
	if (!backend) {
		wlr_log(L_ERROR, "Allocation failed: %s", strerror(errno));
		goto error_state;
	}

	if (!(state->devices = list_create())) {
		wlr_log(L_ERROR, "Allocation failed: %s", strerror(errno));
		goto error_backend;
	}

	state->backend = backend;
	state->session = session;
	state->udev = udev;
	state->display = display;

	state->session_signal.notify = session_signal;
	wl_signal_add(&session->session_signal, &state->session_signal);

	return backend;
error_state:
	free(state);
error_backend:
	wlr_backend_destroy(backend);
	return NULL;
}
