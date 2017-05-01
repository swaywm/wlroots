#ifndef SESSION_H
#define SESSION_H

#include <systemd/sd-bus.h>
#include <stdbool.h>

struct otd_session {
	char *id;
	char *path;
	char *seat;

	sd_bus *bus;
};

struct otd;
bool otd_new_session(struct otd *otd);
void otd_close_session(struct otd *otd);

int take_device(struct otd *restrict otd,
		       const char *restrict path,
		       bool *restrict paused_out);

void release_device(struct otd *otd, int fd);

#endif
