#include <xcb/xfixes.h>
#include "wlr/util/log.h"
#include "xwm.h"

static void xwm_handle_selection_notify(struct wlr_xwm *xwm, xcb_generic_event_t
		*event) {
	wlr_log(L_DEBUG, "TODO: SELECTION NOTIFY");
}

static int xwm_handle_selection_property_notify(struct wlr_xwm *xwm,
		xcb_generic_event_t *event) {
	xcb_property_notify_event_t *property_notify =
		(xcb_property_notify_event_t *) event;

	if (property_notify->window == xwm->selection_window) {
		if (property_notify->state == XCB_PROPERTY_NEW_VALUE &&
				property_notify->atom == xwm->atoms[WL_SELECTION] &&
				xwm->incr)
			wlr_log(L_DEBUG, "TODO: get selection");
			return 1;
	} else if (property_notify->window == xwm->selection_request.requestor) {
		if (property_notify->state == XCB_PROPERTY_DELETE &&
				property_notify->atom == xwm->selection_request.property &&
				xwm->incr)
			wlr_log(L_DEBUG, "TODO: send selection");
			return 1;
	}

	return 0;
}

static void xwm_handle_selection_request(struct wlr_xwm *xwm,
		xcb_generic_event_t *event) {
	wlr_log(L_DEBUG, "TODO: SELECTION REQUEST");
	return;
}

static int weston_wm_handle_xfixes_selection_notify(struct wlr_xwm *xwm,
		xcb_generic_event_t *event) {
	wlr_log(L_DEBUG, "TODO: XFIXES SELECTION NOTIFY");
	return 1;
}


int xwm_handle_selection_event(struct wlr_xwm *xwm,
		xcb_generic_event_t *event) {
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

void xwm_selection_init(struct wlr_xwm *xwm) {
	uint32_t values[1], mask;

	xwm->selection_request.requestor = XCB_NONE;

	values[0] = XCB_EVENT_MASK_PROPERTY_CHANGE;
	xwm->selection_window = xcb_generate_id(xwm->xcb_conn);
	xcb_create_window(xwm->xcb_conn,
		XCB_COPY_FROM_PARENT,
		xwm->selection_window,
		xwm->screen->root,
		0, 0,
		10, 10,
		0,
		XCB_WINDOW_CLASS_INPUT_OUTPUT,
		xwm->screen->root_visual,
		XCB_CW_EVENT_MASK, values);

	xcb_set_selection_owner(xwm->xcb_conn,
		xwm->selection_window,
		xwm->atoms[CLIPBOARD_MANAGER],
		XCB_TIME_CURRENT_TIME);

	mask =
		XCB_XFIXES_SELECTION_EVENT_MASK_SET_SELECTION_OWNER |
		XCB_XFIXES_SELECTION_EVENT_MASK_SELECTION_WINDOW_DESTROY |
		XCB_XFIXES_SELECTION_EVENT_MASK_SELECTION_CLIENT_CLOSE;
	xcb_xfixes_select_selection_input(xwm->xcb_conn, xwm->selection_window,
		xwm->atoms[CLIPBOARD], mask);
}
