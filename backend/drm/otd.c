#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

#include "otd.h"
#include "drm.h"
#include "event.h"
#include "session.h"
#include "udev.h"

struct otd *otd_start(void)
{
	struct otd *otd = calloc(1, sizeof *otd);
	if (!otd)
		return NULL;

	if (!otd_new_session(otd)) {
		fprintf(stderr, "Could not create session\n");
		goto error;
	}

	if (!otd_udev_start(otd)) {
		fprintf(stderr, "Could not start udev\n");
		goto error_session;
	}

	otd_udev_find_gpu(otd);
	if (otd->fd == -1) {
		fprintf(stderr, "Could not open GPU\n");
		goto error_udev;
	}

	if (!init_renderer(otd)) {
		fprintf(stderr, "Could not initalise renderer\n");
		goto error_fd;
	}

	scan_connectors(otd);

	return otd;

error_fd:
	release_device(otd, otd->fd);
error_udev:
	otd_udev_finish(otd);
error_session:
	otd_close_session(otd);
error:
	free(otd);
	return NULL;
}

void otd_finish(struct otd *otd)
{
	if (!otd)
		return;

	for (size_t i = 0; i < otd->display_len; ++i) {
		destroy_display_renderer(otd, &otd->displays[i]);
	}

	destroy_renderer(otd);
	otd_close_session(otd);
	otd_udev_finish(otd);

	close(otd->fd);
	free(otd->events);
	free(otd->displays);
	free(otd);
}

