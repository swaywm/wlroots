#ifndef TYPES_WLR_OUTPUT_SWAPCHAIN_H
#define TYPES_WLR_OUTPUT_SWAPCHAIN_H

#include <wlr/types/wlr_output_swapchain.h>

struct wlr_output_swapchain_manager *wlr_output_swapchain_manager_create(
	struct wlr_renderer *renderer, struct wlr_allocator *allocator);

#endif
