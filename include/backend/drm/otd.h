#ifndef LIBOTD_H
#define LIBOTD_H

#include <stdbool.h>
#include <stddef.h>
#include <EGL/egl.h>
#include <gbm.h>
#include <libudev.h>

#include "session.h"

struct otd {
	int fd;
	bool paused;

	// Priority Queue (Max-heap)
	size_t event_cap;
	size_t event_len;
	struct otd_event *events;

	size_t display_len;
	struct otd_display *displays;

	uint32_t taken_crtcs;

	struct gbm_device *gbm;
	struct {
		EGLDisplay disp;
		EGLConfig conf;
		EGLContext context;
	} egl;

	struct otd_session session;

	struct udev *udev;
	struct udev_monitor *mon;
	int udev_fd;
	char *drm_path;
};

struct otd *otd_start(void);
void otd_finish(struct otd *otd);

#endif
