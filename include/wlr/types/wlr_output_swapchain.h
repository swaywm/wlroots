/*
 * This an unstable interface of wlroots. No guarantees are made regarding the
 * future consistency of this API.
 */
#ifndef WLR_USE_UNSTABLE
#error "Add -DWLR_USE_UNSTABLE to enable unstable wlroots features"
#endif

#ifndef WLR_TYPES_WLR_OUTPUT_SWAPCHAIN_H
#define WLR_TYPES_WLR_OUTPUT_SWAPCHAIN_H

#include <wayland-util.h>

struct wlr_output;

struct wlr_output_swapchain_manager {
	struct wlr_renderer *renderer;

	// private state

	struct wlr_allocator *allocator;
};

struct wlr_output_swapchain {
	struct wlr_output *output;
	struct wlr_output_swapchain_manager *manager;
	struct wlr_swapchain *swapchain;

	// private state

	struct wlr_buffer *back_buffer;

	struct wl_listener output_destroy;
};

struct wlr_output_swapchain_manager *wlr_output_swapchain_manager_autocreate(
	struct wlr_backend *backend);
void wlr_output_swapchain_manager_destroy(
	struct wlr_output_swapchain_manager *manager);

struct wlr_output_swapchain *wlr_output_swapchain_create(
	struct wlr_output_swapchain_manager *manager, struct wlr_output *output);
void wlr_output_swapchain_destroy(struct wlr_output_swapchain *output_swapchain);
bool wlr_output_swapchain_begin(struct wlr_output_swapchain *output_swapchain,
	int *buffer_age);
void wlr_output_swapchain_end(struct wlr_output_swapchain *output_swapchain);

#endif
