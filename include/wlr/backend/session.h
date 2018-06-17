#ifndef WLR_BACKEND_SESSION_H
#define WLR_BACKEND_SESSION_H

#include <libudev.h>
#include <stdbool.h>
#include <sys/types.h>
#include <wayland-server.h>

struct session_impl;

struct wlr_device {
	int fd;
	dev_t dev;
	struct wl_signal signal;

	struct wl_list link;
};

struct wlr_session {
	const struct session_impl *impl;
	/*
	 * Signal for when the session becomes active/inactive.
	 * It's called when we swap virtual terminal.
	 */
	struct wl_signal session_signal;
	bool active;

	/*
	 * 0 if virtual terminals are not supported
	 * i.e. seat != "seat0"
	 */
	unsigned vtnr;
	char seat[256];

	struct udev *udev;
	struct udev_monitor *mon;
	struct wl_event_source *udev_event;

	struct wl_list devices;

	struct wl_listener display_destroy;
};

/*
 * Opens a session, taking control of the current virtual terminal.
 * This should not be called if another program is already in control
 * of the terminal (Xorg, another Wayland compositor, etc.).
 *
 * If logind support is not enabled, you must have CAP_SYS_ADMIN or be root.
 * It is safe to drop privileges after this is called.
 *
 * Returns NULL on error.
 */
struct wlr_session *wlr_session_create(struct wl_display *disp);

/*
 * Closes a previously opened session and restores the virtual terminal.
 * You should call wlr_session_close_file on each files you opened
 * with wlr_session_open_file before you call this.
 */
void wlr_session_destroy(struct wlr_session *session);

/*
 * Opens the file at path.
 * This can only be used to open DRM or evdev (input) devices.
 *
 * When the session becomes inactive:
 * - DRM files lose their DRM master status
 * - evdev files become invalid and should be closed
 *
 * Returns -errno on error.
 */
int wlr_session_open_file(struct wlr_session *session, const char *path);

/*
 * Closes a file previously opened with wlr_session_open_file.
 */
void wlr_session_close_file(struct wlr_session *session, int fd);

void wlr_session_signal_add(struct wlr_session *session, int fd,
	struct wl_listener *listener);
/*
 * Changes the virtual terminal.
 */
bool wlr_session_change_vt(struct wlr_session *session, unsigned vt);

size_t wlr_session_find_gpus(struct wlr_session *session,
	size_t ret_len, int *ret);

#endif
