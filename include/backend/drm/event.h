#ifndef EVENT_H
#define EVENT_H

#include <stdbool.h>

enum otd_event_type {
	OTD_EV_NONE,
	OTD_EV_RENDER,
	OTD_EV_DISPLAY_REM,
	OTD_EV_DISPLAY_ADD,
};

struct otd_event {
	enum otd_event_type type;
	struct otd_display *display;
};

bool otd_get_event(struct otd *otd, struct otd_event *restrict ret);
bool event_add(struct otd *otd, struct otd_display *disp, enum otd_event_type type);

#endif
