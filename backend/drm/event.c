#include <stdbool.h>
#include <stdlib.h>
#include <poll.h>

#include "backend/drm/backend.h"
#include "backend/drm/event.h"
#include "backend/drm/drm.h"
#include "backend/drm/udev.h"

static inline void event_swap(struct wlr_drm_event *a, struct wlr_drm_event *b)
{
	struct wlr_drm_event tmp = *a;
	*a = *b;
	*b = tmp;
}

bool wlr_drm_get_event(struct wlr_drm_backend *backend,
		struct wlr_drm_event *restrict ret)
{
	struct pollfd fds[] = {
		{ .fd = backend->fd, .events = POLLIN },
		{ .fd = backend->udev.mon_fd, .events = POLLIN },
	};

	while (poll(fds, 2, 0) > 0) {
		if (fds[0].revents)
			wlr_drm_event(backend->fd);
		if (fds[1].revents)
			wlr_udev_event(backend);
	}

	if (backend->event_len == 0) {
		ret->type = DRM_EV_NONE;
		ret->display = NULL;
		return false;
	}

	struct wlr_drm_event *ev = backend->events;

	// Downheap
	*ret = ev[0];
	ev[0] = ev[--backend->event_len];

	size_t i = 0;
	while (i < backend->event_len / 2) {
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

bool wlr_drm_add_event(struct wlr_drm_backend *backend,
		struct wlr_drm_display *disp, enum wlr_drm_event_type type)
{
	if (type == DRM_EV_NONE)
		return true;

	if (backend->event_len == backend->event_cap) {
		size_t new_size = (backend->event_cap == 0) ? 8 : backend->event_cap * 2;

		struct wlr_drm_event *new = realloc(backend->events, sizeof *new * new_size);
		if (!new) {
			return false;
		}

		backend->event_cap = new_size;
		backend->events = new;
	}

	struct wlr_drm_event *ev = backend->events;

	// Upheap
	size_t i = backend->event_len++;
	ev[i].type = type;
	ev[i].display = disp;

	size_t j;
	while (i > 0 && ev[i].type > ev[(j = (i - 1) / 2)].type) {
		event_swap(&ev[i], &ev[j]);
		i = j;
	}

	return true;
}
