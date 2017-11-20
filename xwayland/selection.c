#include <xcb/xfixes.h>
#include "wlr/util/log.h"
#include "xwm.h"

static void xwm_handle_selection_notify(struct wlr_xwm *xwm, xcb_generic_event_t
		*event) {
	wlr_log(L_DEBUG, "TODO: SELECTION NOTIFY");
}

static int xwm_handle_selection_property_notify(struct wlr_xwm *xwm,
		xcb_generic_event_t *event) {
	wlr_log(L_DEBUG, "TODO: SELECTION PROPERTY NOTIFY");
	return 1;
}

static void xwm_handle_selection_request(struct wlr_xwm *xwm, xcb_generic_event_t
		*event) {
	wlr_log(L_DEBUG, "TODO: SELECTION REQUEST");
	return;
}

static int weston_wm_handle_xfixes_selection_notify(struct wlr_xwm *xwm,
		xcb_generic_event_t *event) {
	wlr_log(L_DEBUG, "TODO: XFIXES SELECTION NOTIFY");
	return 1;
}


int xwm_handle_selection_event(struct wlr_xwm *xwm, xcb_generic_event_t *event) {
	switch (event->response_type & ~0x80) {
	case XCB_SELECTION_NOTIFY:
		xwm_handle_selection_notify(xwm, event);
		return 1;
	case XCB_PROPERTY_NOTIFY:
		return xwm_handle_selection_property_notify(xwm, event);
	case XCB_SELECTION_REQUEST:
		xwm_handle_selection_request(xwm, event);
		return 1;
	}

	switch (event->response_type - xwm->xfixes->first_event) {
	case XCB_XFIXES_SELECTION_NOTIFY:
		return weston_wm_handle_xfixes_selection_notify(xwm, event);
	}

	return 0;
}
