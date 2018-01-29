#ifndef BACKEND_HEADLESS_H
#define BACKEND_HEADLESS_H

#include <wlr/backend/interface.h>
#include <wlr/backend/headless.h>

struct wlr_headless_backend {
	struct wlr_backend backend;
	struct wlr_egl egl;
	struct wlr_renderer *renderer;
	struct wl_display *display;
	struct wl_list outputs;
	struct wl_list input_devices;
	struct wl_listener display_destroy;
	bool started;
};

struct wlr_headless_output {
	struct wlr_output wlr_output;

	struct wlr_headless_backend *backend;
	struct wl_list link;

	void *egl_surface;
	struct wl_event_source *frame_timer;
	int frame_delay; // ms
};

struct wlr_headless_input_device {
	struct wlr_input_device wlr_input_device;

	struct wlr_headless_backend *backend;
};

#endif
