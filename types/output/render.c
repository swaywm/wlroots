#include <assert.h>
#include <drm_fourcc.h>
#include <stdlib.h>
#include <wlr/interfaces/wlr_output.h>
#include <wlr/render/interface.h>
#include <wlr/util/log.h>
#include "allocator/allocator.h"
#include "backend/backend.h"
#include "render/drm_format_set.h"
#include "render/swapchain.h"
#include "render/wlr_renderer.h"
#include "types/wlr_output.h"

/**
 * Ensure the output has a suitable swapchain. The swapchain is re-created if
 * necessary.
 *
 * If allow_modifiers is set to true, the swapchain's format may use modifiers.
 * If set to false, the swapchain's format is guaranteed to not use modifiers.
 */
static bool output_create_swapchain(struct wlr_output *output,
		bool allow_modifiers) {
	int width, height;
	output_pending_resolution(output, &width, &height);

	if (output->swapchain != NULL && output->swapchain->width == width &&
			output->swapchain->height == height &&
			(allow_modifiers || output->swapchain->format->len == 0)) {
		return true;
	}

	struct wlr_allocator *allocator = output->allocator;
	if (allocator == NULL) {
		wlr_log(WLR_ERROR, "No allocator available");
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
	} else {
		wlr_log(WLR_ERROR, "No output display formats available");
		return false;
	}

	struct wlr_swapchain *swapchain = wlr_allocator_create_swapchain(allocator,
		width, height, display_formats, allow_modifiers);
	if (swapchain == NULL) {
		wlr_log(WLR_ERROR, "Failed to create output swapchain");
		return false;
	}

	wlr_swapchain_destroy(output->swapchain);
	output->swapchain = swapchain;

	return true;
}

static bool output_attach_back_buffer(struct wlr_output *output,
		int *buffer_age) {
	assert(output->back_buffer == NULL);

	if (!output_create_swapchain(output, true)) {
		return false;
	}

	struct wlr_renderer *renderer = wlr_backend_get_renderer(output->backend);
	assert(renderer != NULL);

	struct wlr_buffer *buffer =
		wlr_swapchain_acquire(output->swapchain, buffer_age);
	if (buffer == NULL) {
		return false;
	}

	if (!renderer_bind_buffer(renderer, buffer)) {
		wlr_buffer_unlock(buffer);
		return false;
	}

	output->back_buffer = buffer;
	return true;
}

void output_clear_back_buffer(struct wlr_output *output) {
	if (output->back_buffer == NULL) {
		return;
	}

	struct wlr_renderer *renderer = wlr_backend_get_renderer(output->backend);
	assert(renderer != NULL);

	renderer_bind_buffer(renderer, NULL);

	wlr_buffer_unlock(output->back_buffer);
	output->back_buffer = NULL;
}

bool wlr_output_attach_render(struct wlr_output *output, int *buffer_age) {
	if (!output_attach_back_buffer(output, buffer_age)) {
		return false;
	}
	wlr_output_attach_buffer(output, output->back_buffer);
	return true;
}

static bool output_attach_empty_buffer(struct wlr_output *output) {
	assert(!(output->pending.committed & WLR_OUTPUT_STATE_BUFFER));

	if (!wlr_output_attach_render(output, NULL)) {
		return false;
	}

	int width, height;
	output_pending_resolution(output, &width, &height);

	struct wlr_renderer *renderer = wlr_backend_get_renderer(output->backend);
	wlr_renderer_begin(renderer, width, height);
	wlr_renderer_clear(renderer, (float[]){0, 0, 0, 0});
	wlr_renderer_end(renderer);

	return true;
}

bool output_ensure_buffer(struct wlr_output *output) {
	// If we're lighting up an output or changing its mode, make sure to
	// provide a new buffer
	bool needs_new_buffer = false;
	if ((output->pending.committed & WLR_OUTPUT_STATE_ENABLED) &&
			output->pending.enabled) {
		needs_new_buffer = true;
	}
	if (output->pending.committed & WLR_OUTPUT_STATE_MODE) {
		needs_new_buffer = true;
	}
	if (!needs_new_buffer ||
			(output->pending.committed & WLR_OUTPUT_STATE_BUFFER)) {
		return true;
	}

	// If the backend doesn't necessarily need a new buffer on modeset, don't
	// bother allocating one.
	if (!output->impl->test || output->impl->test(output)) {
		return true;
	}

	wlr_log(WLR_DEBUG, "Attaching empty buffer to output for modeset");

	if (!output_attach_empty_buffer(output)) {
		goto error;
	}
	if (!output->impl->test || output->impl->test(output)) {
		return true;
	}

	output_clear_back_buffer(output);
	output->pending.committed &= ~WLR_OUTPUT_STATE_BUFFER;

	if (output->swapchain->format->len == 0) {
		return false;
	}

	// The test failed for a buffer which has modifiers, try disabling
	// modifiers to see if that makes a difference.
	wlr_log(WLR_DEBUG, "Output modeset test failed, retrying without modifiers");

	if (!output_create_swapchain(output, false)) {
		return false;
	}
	if (!output_attach_empty_buffer(output)) {
		goto error;
	}
	if (!output->impl->test(output)) {
		goto error;
	}
	return true;

error:
	output_clear_back_buffer(output);
	output->pending.committed &= ~WLR_OUTPUT_STATE_BUFFER;
	return false;
}

void wlr_output_lock_attach_render(struct wlr_output *output, bool lock) {
	if (lock) {
		++output->attach_render_locks;
	} else {
		assert(output->attach_render_locks > 0);
		--output->attach_render_locks;
	}
	wlr_log(WLR_DEBUG, "%s direct scan-out on output '%s' (locks: %d)",
		lock ? "Disabling" : "Enabling", output->name,
		output->attach_render_locks);
}

uint32_t wlr_output_preferred_read_format(struct wlr_output *output) {
	struct wlr_renderer *renderer = wlr_backend_get_renderer(output->backend);
	if (!renderer->impl->preferred_read_format || !renderer->impl->read_pixels) {
		return DRM_FORMAT_INVALID;
	}

	if (!output_attach_back_buffer(output, NULL)) {
		return false;
	}

	uint32_t fmt = renderer->impl->preferred_read_format(renderer);

	output_clear_back_buffer(output);

	return fmt;
}
