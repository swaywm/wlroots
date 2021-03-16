#ifndef XWAYLAND_TRACE_H
#define XWAYLAND_TRACE_H

#include <xcb/xcb.h>
#include <wayland-util.h>

struct wlr_x11_trace {
	struct wl_list trace_events; // trace_event::link
};

void wlr_x11_trace_init(struct wlr_x11_trace *trace);
void wlr_x11_trace_deinit(struct wlr_x11_trace *trace);

void wlr_x11_trace_received_event(struct wlr_x11_trace *trace, xcb_generic_event_t *event);
void wlr_x11_trace_log_error_trace(struct wlr_x11_trace *trace, uint32_t sequence);

void wlr_x11_trace_trace(struct wlr_x11_trace *trace, unsigned int sequence,
	const char *file, unsigned int line);

#define WLR_X11_TRACE(trace, conn) \
	wlr_x11_trace_trace((trace), xcb_no_operation(conn).sequence, __FILE__, __LINE__)

#endif
