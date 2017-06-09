#include <stdlib.h>
#include <assert.h>
#include <libinput.h>
#include <wlr/session.h>
#include <wlr/backend/interface.h>
#include "backend/udev.h"
#include "backend/libinput/backend.h"
#include "common/log.h"

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

static int wlr_libinput_handle_event(int fd, uint32_t mask, void *_state) {
	// TODO
	return 0;
}

static void wlr_libinput_log(struct libinput *libinput,
		enum libinput_log_priority priority, const char *fmt, va_list args) {
	_wlr_vlog(L_ERROR, fmt, args);
}

static bool wlr_libinput_backend_init(struct wlr_backend_state *state) {
	state->handle = libinput_udev_create_context(&libinput_impl, state,
			state->udev->udev);
	if (!state->handle) {
		return false;
	}

	// TODO: Let user customize seat used
	if (!libinput_udev_assign_seat(state->handle, "seat0")) {
		return false;
	}

	// TODO: More sophisticated logging
	libinput_log_set_handler(state->handle, wlr_libinput_log);
	libinput_log_set_priority(state->handle, LIBINPUT_LOG_PRIORITY_ERROR);

	struct wl_event_loop *event_loop =
		wl_display_get_event_loop(state->display);
	if (state->input_event) {
		wl_event_source_remove(state->input_event);
	}
	state->input_event = wl_event_loop_add_fd(event_loop,
			libinput_get_fd(state->handle), WL_EVENT_READABLE,
			wlr_libinput_handle_event, state);
	if (!state->input_event) {
		return false;
	}
	return true;
}

static void wlr_libinput_backend_destroy(struct wlr_backend_state *state) {
	// TODO
}

static struct wlr_backend_impl backend_impl = {
	.init = wlr_libinput_backend_init,
	.destroy = wlr_libinput_backend_destroy
};

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
		return NULL;
	}

	state->backend = backend;
	state->session = session;
	state->udev = udev;
	state->display = display;

	return backend;
}
