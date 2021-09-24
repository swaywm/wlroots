#include <assert.h>
#include <drm_fourcc.h>
#include <stdlib.h>
#include <wlr/backend.h>
#include <wlr/interfaces/wlr_output.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_swapchain.h>
#include <wlr/util/log.h>
#include "backend/backend.h"
#include "render/allocator/allocator.h"
#include "render/drm_format_set.h"
#include "render/swapchain.h"
#include "render/wlr_renderer.h"
#include "types/wlr_output_swapchain.h"

struct wlr_output_swapchain_manager *wlr_output_swapchain_manager_create(
		struct wlr_renderer *renderer, struct wlr_allocator *allocator) {
	struct wlr_output_swapchain_manager *manager = calloc(1, sizeof(*manager));
	if (manager == NULL) {
		return NULL;
	}

	manager->renderer = renderer;
	manager->allocator = allocator;

	return manager;
}

struct wlr_output_swapchain_manager *wlr_output_swapchain_manager_autocreate(
		struct wlr_backend *backend) {
	struct wlr_renderer *renderer = wlr_renderer_autocreate(backend);
	if (renderer == NULL) {
		return NULL;
	}

	struct wlr_allocator *allocator = wlr_allocator_autocreate(backend, renderer);
	if (allocator == NULL) {
		wlr_renderer_destroy(renderer);
		return NULL;
	}

	struct wlr_output_swapchain_manager *manager =
		wlr_output_swapchain_manager_create(renderer, allocator);
	if (manager == NULL) {
		wlr_allocator_destroy(allocator);
		wlr_renderer_destroy(renderer);
		return NULL;
	}

	return manager;
}

void wlr_output_swapchain_manager_destroy(
		struct wlr_output_swapchain_manager *manager) {
	wlr_allocator_destroy(manager->allocator);
	wlr_renderer_destroy(manager->renderer);
	free(manager);
}

static void output_pending_resolution(struct wlr_output *output, int *width,
		int *height) {
	if (output->pending.committed & WLR_OUTPUT_STATE_MODE) {
		switch (output->pending.mode_type) {
		case WLR_OUTPUT_STATE_MODE_FIXED:
			*width = output->pending.mode->width;
			*height = output->pending.mode->height;
			return;
		case WLR_OUTPUT_STATE_MODE_CUSTOM:
			*width = output->pending.custom_mode.width;
			*height = output->pending.custom_mode.height;
			return;
		}
		abort();
	} else {
		*width = output->width;
		*height = output->height;
	}
}

static struct wlr_drm_format *pick_format(
		struct wlr_output_swapchain *output_swapchain,
		const struct wlr_drm_format_set *display_formats) {
	struct wlr_renderer *renderer = output_swapchain->manager->renderer;

	const struct wlr_drm_format_set *render_formats =
		wlr_renderer_get_render_formats(renderer);
	if (render_formats == NULL) {
		wlr_log(WLR_ERROR, "Failed to get render formats");
		return NULL;
	}

	struct wlr_drm_format *format = NULL;
	const uint32_t candidates[] = { DRM_FORMAT_ARGB8888, DRM_FORMAT_XRGB8888 };
	for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
		uint32_t fmt = candidates[i];

		const struct wlr_drm_format *render_format =
			wlr_drm_format_set_get(render_formats, fmt);
		if (render_format == NULL) {
			wlr_log(WLR_DEBUG, "Renderer doesn't support format 0x%"PRIX32, fmt);
			continue;
		}

		if (display_formats != NULL) {
			const struct wlr_drm_format *display_format =
				wlr_drm_format_set_get(display_formats, fmt);
			if (display_format == NULL) {
				wlr_log(WLR_DEBUG, "Output doesn't support format 0x%"PRIX32, fmt);
				continue;
			}
			format = wlr_drm_format_intersect(display_format, render_format);
		} else {
			// The output can display any format
			format = wlr_drm_format_dup(render_format);
		}

		if (format == NULL) {
			wlr_log(WLR_DEBUG, "Failed to intersect display and render "
				"modifiers for format 0x%"PRIX32, fmt);
		} else {
			break;
		}
	}
	if (format == NULL) {
		wlr_log(WLR_ERROR, "Failed to choose a format for output '%s'",
			output_swapchain->output->name);
		return NULL;
	}

	return format;
}

static bool update_swapchain(struct wlr_output_swapchain *output_swapchain,
		bool allow_modifiers) {
	struct wlr_output *output = output_swapchain->output;

	int width, height;
	output_pending_resolution(output, &width, &height);

	if (output_swapchain->swapchain != NULL &&
			output_swapchain->swapchain->width == width &&
			output_swapchain->swapchain->height == height &&
			(allow_modifiers || output->swapchain->format->len == 0)) {
		return true;
	}

	struct wlr_allocator *allocator = output_swapchain->manager->allocator;
	if (allocator == NULL) {
		wlr_log(WLR_ERROR, "Failed to get backend allocator");
		return false;
	}

	const struct wlr_drm_format_set *display_formats = NULL;
	if (output->impl->get_primary_formats) {
		display_formats =
			output->impl->get_primary_formats(output, allocator->buffer_caps);
		if (display_formats == NULL) {
			wlr_log(WLR_ERROR, "Failed to get primary display formats");
			return false;
		}
	}

	struct wlr_drm_format *format =
		pick_format(output_swapchain, display_formats);
	if (format == NULL) {
		wlr_log(WLR_ERROR, "Failed to pick primary buffer format for output '%s'",
			output->name);
		return false;
	}
	wlr_log(WLR_DEBUG, "Choosing primary buffer format 0x%"PRIX32" for output '%s'",
		format->format, output->name);

	if (!allow_modifiers && (format->len != 1 || format->modifiers[0] != DRM_FORMAT_MOD_LINEAR)) {
		format->len = 0;
	}

	struct wlr_swapchain *swapchain =
		wlr_swapchain_create(allocator, width, height, format);
	free(format);
	if (swapchain == NULL) {
		wlr_log(WLR_ERROR, "Failed to create output swapchain");
		return false;
	}

	wlr_swapchain_destroy(output_swapchain->swapchain);
	output_swapchain->swapchain = swapchain;
	return true;
}

static void handle_output_destroy(struct wl_listener *listener, void *data) {
	struct wlr_output_swapchain *output_swapchain =
		wl_container_of(listener, output_swapchain, output_destroy);
	wlr_output_swapchain_destroy(output_swapchain);
}

struct wlr_output_swapchain *wlr_output_swapchain_create(
		struct wlr_output_swapchain_manager *manager,
		struct wlr_output *output) {
	struct wlr_output_swapchain *output_swapchain =
		calloc(1, sizeof(*output_swapchain));
	if (output_swapchain == NULL) {
		return NULL;
	}

	output_swapchain->output = output;
	output_swapchain->manager = manager;

	output_swapchain->output_destroy.notify = handle_output_destroy;
	wl_signal_add(&output->events.destroy, &output_swapchain->output_destroy);

	return output_swapchain;
}

void wlr_output_swapchain_destroy(struct wlr_output_swapchain *output_swapchain) {
	wl_list_remove(&output_swapchain->output_destroy.link);
	free(output_swapchain);
}

bool wlr_output_swapchain_begin(struct wlr_output_swapchain *output_swapchain,
		int *buffer_age) {
	assert(output_swapchain->back_buffer == NULL);

	if (!update_swapchain(output_swapchain, true)) {
		return false;
	}

	struct wlr_buffer *buffer =
		wlr_swapchain_acquire(output_swapchain->swapchain, buffer_age);
	if (buffer == NULL) {
		return false;
	}

	if (!wlr_renderer_begin_with_buffer(output_swapchain->manager->renderer,
			buffer)) {
		wlr_buffer_unlock(buffer);
		return false;
	}

	output_swapchain->back_buffer = buffer;
	return true;
}

void wlr_output_swapchain_end(struct wlr_output_swapchain *output_swapchain) {
	assert(output_swapchain->back_buffer != NULL);

	wlr_renderer_end(output_swapchain->manager->renderer);

	wlr_output_attach_buffer(output_swapchain->output,
		output_swapchain->back_buffer);

	wlr_buffer_unlock(output_swapchain->back_buffer);
	output_swapchain->back_buffer = NULL;
}
