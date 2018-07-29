/*
 * This an unstable interface of wlroots. No guarantees are made regarding the
 * future consistency of this API.
 */
#ifndef WLR_USE_UNSTABLE
#error "Add -DWLR_USE_UNSTABLE to enable unstable wlroots features"
#endif

#ifndef WLR_TYPES_WLR_SCREENSHOOTER_H
#define WLR_TYPES_WLR_SCREENSHOOTER_H

#include <wayland-server.h>

struct wlr_screenshooter {
	struct wl_global *global;
	struct wl_list screenshots; // wlr_screenshot::link

	struct wl_listener display_destroy;

	void *data;
};

struct wlr_screenshot {
	struct wl_resource *resource;
	struct wl_resource *output_resource;
	struct wl_list link;

	struct wlr_output *output;
	struct wlr_screenshooter *screenshooter;

	void* data;
};

struct wlr_screenshooter *wlr_screenshooter_create(struct wl_display *display);
void wlr_screenshooter_destroy(struct wlr_screenshooter *screenshooter);

#endif
