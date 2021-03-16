#include "xwayland/trace.h"
#include <stdlib.h>
#include <wlr/util/log.h>

struct trace_event {
	const char *file;
	unsigned int line;
	unsigned int sequence;
	struct wl_list link;
};

static void trace_event_free(struct trace_event *event) {
	wl_list_remove(&event->link);
	free(event);
}

void wlr_x11_trace_init(struct wlr_x11_trace *trace) {
	wl_list_init(&trace->trace_events);
}

void wlr_x11_trace_deinit(struct wlr_x11_trace *trace) {
	struct trace_event *event, *next;
	wl_list_for_each_safe(event, next, &trace->trace_events, link) {
		trace_event_free(event);
	}
}

void wlr_x11_trace_trace(struct wlr_x11_trace *trace, unsigned int sequence,
	const char *file, unsigned int line) {
	struct trace_event *event = malloc(sizeof(*event));
	if (event == NULL) {
		return;
	}

	event->file = file;
	event->line = line;
	event->sequence = sequence;

	wl_list_insert(trace->trace_events.prev, &event->link);
}

// Remove entries from the event queue which are no longer needed.
//
// This removes all but one event with an older sequence number than the given
// event.
void wlr_x11_trace_received_event(struct wlr_x11_trace *trace, xcb_generic_event_t *event) {
	struct trace_event *entry, *next, *prev = NULL;
	wl_list_for_each_safe(entry, next, &trace->trace_events, link) {
		if (prev) {
			if (entry->sequence >= event->full_sequence) {
				break;
			}
			trace_event_free(prev);
		}
		prev = entry;
	}
}

void wlr_x11_trace_log_error_trace(struct wlr_x11_trace *trace, uint32_t sequence) {
	struct trace_event *entry, *prev = NULL;
	wl_list_for_each(entry, &trace->trace_events, link) {
		if (entry->sequence >= sequence) {
			if (prev) {
				wlr_log(WLR_ERROR, "X11 error happened somewhere after %s:%d",
						prev->file, prev->line);
			}
			wlr_log(WLR_ERROR, "X11 error happened somewhere before %s:%d",
					entry->file, entry->line);
			return;
		}
		prev = entry;
	}
	if (prev) {
		wlr_log(WLR_ERROR, "X11 error happened somewhere after %s:%d",
				prev->file, prev->line);
	}
}
