#ifndef BACKEND_HEADLESS_H
#define BACKEND_HEADLESS_H

#include <wlr/backend/headless.h>
#include <wlr/backend/interface.h>

#define HEADLESS_DEFAULT_REFRESH (60 * 1000) // 60 Hz

struct wlr_headless_backend {
	struct wlr_backend backend;
	int drm_fd;
	struct wlr_renderer *renderer;
	struct wlr_allocator *allocator;
	struct wlr_drm_format *format;
	struct wl_display *display;
	struct wl_list outputs;
	size_t last_output_num;
	struct wl_list input_devices;
	struct wl_listener display_destroy;
	struct wl_listener renderer_destroy;
	bool has_parent_renderer;
	bool started;
};

struct wlr_headless_output {
	struct wlr_output wlr_output;

	struct wlr_headless_backend *backend;
	struct wl_list link;

	struct wlr_swapchain *swapchain;
	struct wlr_buffer *back_buffer, *front_buffer;

	struct wl_event_source *frame_timer;
	int frame_delay; // ms
};

struct wlr_headless_input_device {
	struct wlr_input_device wlr_input_device;

	struct wlr_headless_backend *backend;
};

struct wlr_headless_backend *headless_backend_from_backend(
	struct wlr_backend *wlr_backend);

#endif
