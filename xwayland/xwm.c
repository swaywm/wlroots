#include <stdlib.h>
#include "wlr/util/log.h"
#include "wlr/types/wlr_surface.h"
#include "wlr/xwayland.h"
#include "xwayland/internals.h"

#define SEND_EVENT_MASK 0x80
static int x11_event_handler(int fd, uint32_t mask, void *data) {
	int count = 0;
	xcb_generic_event_t *event;
	struct wlr_xwayland *wlr_xwayland = data;
	struct wlr_xwm *xwm = wlr_xwayland->xwm;

	while ((event = xcb_poll_for_event(xwm->xcb_connection))) {
		wlr_log(L_DEBUG, "X11 event: %d", event->response_type & ~SEND_EVENT_MASK);
		count++;
		// TODO: actually do stuff!
	}
	return count;
}

static void create_surface_handler(struct wl_listener *listener, void *data)
{
	struct wlr_surface *surface = data;
        struct wlr_xwm *xwm = wl_container_of(listener, xwm, surface_listener);

	if (wl_resource_get_client(surface->resource) != xwm->xwayland->client) {
		return;
	}

	wlr_log(L_DEBUG, "new x11 surface: %p", surface);

	// TODO: look for unpaired window, and assign
}

void xwm_destroy(struct wlr_xwm *xwm) {
	if (xwm->event_source) {
		wl_event_source_remove(xwm->event_source);
	}

	free(xwm);
}

struct wlr_xwm *xwm_create(struct wlr_xwayland *wlr_xwayland) {
	struct wlr_xwm *xwm = calloc(1, sizeof(struct wlr_xwm));

	xwm->xwayland = wlr_xwayland;

	xwm->xcb_connection = xcb_connect_to_fd(wlr_xwayland->wm_fd[0], NULL);
	if (xcb_connection_has_error(xwm->xcb_connection)) {
		return NULL;
	}

	// TODO more xcb init
	// xcb_prefetch_extension_data(xwm->xcb_connection, &xcb_composite_id);

        struct wl_event_loop *event_loop = wl_display_get_event_loop(wlr_xwayland->wl_display);
	xwm->event_source = wl_event_loop_add_fd(event_loop, wlr_xwayland->wm_fd[0],
			WL_EVENT_READABLE, x11_event_handler, wlr_xwayland);
	// probably not needed
	// wl_event_source_check(xwm->event_source);


	xwm->surface_listener.notify = create_surface_handler;
	//wl_signal_add(&wlr_xwayland->compositor->create_surface_signal,
	//		&xwm->surface_listener);

	return xwm;
}
