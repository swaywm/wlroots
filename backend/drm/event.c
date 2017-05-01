#include "otd.h"
#include "event.h"
#include "drm.h"
#include "udev.h"

#include <stdbool.h>
#include <stdlib.h>
#include <poll.h>

static inline void event_swap(struct otd_event *a, struct otd_event *b)
{
	struct otd_event tmp = *a;
	*a = *b;
	*b = tmp;
}

bool otd_get_event(struct otd *otd, struct otd_event *restrict ret)
{
	struct pollfd fds[] = {
		{ .fd = otd->fd, .events = POLLIN },
		{ .fd = otd->udev_fd, .events = POLLIN },
	};

	while (poll(fds, 2, 0) > 0) {
		if (fds[0].revents)
			get_drm_event(otd);
		if (fds[1].revents)
			otd_udev_event(otd);
	}

	if (otd->event_len == 0) {
		ret->type = OTD_EV_NONE;
		ret->display = NULL;
		return false;
	}

	struct otd_event *ev = otd->events;

	// Downheap
	*ret = ev[0];
	ev[0] = ev[--otd->event_len];

	size_t i = 0;
	while (i < otd->event_len / 2) {
		size_t left = i * 2 + 1;
		size_t right = i * 2 + 2;
		size_t max = (ev[left].type > ev[right].type) ? left : right;

		if (ev[i].type <= ev[max].type) {
			event_swap(&ev[i], &ev[max]);
			i = max;
		} else {
			break;
		}
	}

	return true;
}

bool event_add(struct otd *otd, struct otd_display *disp, enum otd_event_type type)
{
	if (type == OTD_EV_NONE)
		return true;

	if (otd->event_len == otd->event_cap) {
		size_t new_size = (otd->event_cap == 0) ? 8 : otd->event_cap * 2;

		struct otd_event *new = realloc(otd->events, sizeof *new * new_size);
		if (!new) {
			return false;
		}

		otd->event_cap = new_size;
		otd->events = new;
	}

	struct otd_event *ev = otd->events;

	// Upheap
	size_t i = otd->event_len++;
	ev[i].type = type;
	ev[i].display = disp;

	size_t j;
	while (i > 0 && ev[i].type > ev[(j = (i - 1) / 2)].type) {
		event_swap(&ev[i], &ev[j]);
		i = j;
	}

	return true;
}
