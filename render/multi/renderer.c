#include <assert.h>
#include <stdlib.h>
#include <wlr/render/interface.h>
#include <wlr/util/log.h>
#include "render/multi.h"

static struct wlr_multi_renderer *renderer_get_multi(
		struct wlr_renderer *wlr_renderer) {
	assert(wlr_renderer_is_multi(wlr_renderer));
	return (struct wlr_multi_renderer *)wlr_renderer;
}

static void renderer_attempt_render() {
	assert(false); // Multi renderer can only be used to create textures
}

static bool renderer_attempt_render_texture(struct wlr_renderer *wlr_renderer,
		struct wlr_texture *texture, const float matrix[static 9],
		float alpha) {
	renderer_attempt_render();
	return false;
}

// https://www.geeksforgeeks.org/move-zeroes-end-array/
static size_t push_zeroes_to_end(uint32_t arr[], size_t n) {
	size_t count = 0;
	for (size_t i = 0; i < n; i++) {
		if (arr[i] != 0) {
			arr[count++] = arr[i];
		}
	}

	size_t ret = count;
	while (count < n) {
		arr[count++] = 0;
	}

	return ret;
}

static void multi_renderer_update_formats(struct wlr_multi_renderer *renderer) {
	free(renderer->formats);
	renderer->formats = NULL;
	renderer->formats_len = 0;
	if (wl_list_empty(&renderer->children)) {
		return;
	}

	size_t len;
	struct wlr_multi_renderer_child *first_child =
		wl_container_of(renderer->children.prev, first_child, link);
	const enum wl_shm_format *first_child_formats =
		wlr_renderer_get_formats(first_child->renderer, &len);

	renderer->formats = malloc(len * sizeof(enum wl_shm_format));
	if (renderer->formats == NULL) {
		return;
	}
	renderer->formats_len = len;
	memcpy(renderer->formats, first_child_formats,
		len * sizeof(enum wl_shm_format));

	// Restrict to the subset of formats supported by all renderers
	struct wlr_multi_renderer_child *child;
	wl_list_for_each(child, &renderer->children, link) {
		if (child == first_child) {
			continue;
		}

		size_t child_formats_len;
		const enum wl_shm_format *child_formats =
			wlr_renderer_get_formats(child->renderer, &child_formats_len);

		for (size_t i = 0; i < renderer->formats_len; ++i) {
			enum wl_shm_format fmt = renderer->formats[i];

			bool child_supports = false;
			for (size_t j = 0; j < child_formats_len; ++j) {
				if (fmt == child_formats[j]) {
					child_supports = true;
					break;
				}
			}

			if (!child_supports) {
				renderer->formats[i] = 0;
			}
		}
	}

	renderer->formats_len =
		push_zeroes_to_end(renderer->formats, renderer->formats_len);
}

static const enum wl_shm_format *renderer_get_formats(
		struct wlr_renderer *wlr_renderer, size_t *len) {
	struct wlr_multi_renderer *renderer = renderer_get_multi(wlr_renderer);

	*len = renderer->formats_len;
	return renderer->formats;
}

static bool renderer_format_supported(struct wlr_renderer *wlr_renderer,
		enum wl_shm_format fmt) {
	struct wlr_multi_renderer *renderer = renderer_get_multi(wlr_renderer);

	struct wlr_multi_renderer_child *child;
	wl_list_for_each(child, &renderer->children, link) {
		if (!wlr_renderer_format_supported(child->renderer, fmt)) {
			return false;
		}
	}

	return true;
}

static struct wlr_texture *renderer_texture_from_pixels(
		struct wlr_renderer *wlr_renderer, enum wl_shm_format fmt,
		uint32_t stride, uint32_t width, uint32_t height, const void *data) {
	struct wlr_multi_renderer *renderer = renderer_get_multi(wlr_renderer);

	if (wl_list_empty(&renderer->children)) {
		return NULL;
	}

	struct wlr_multi_texture *texture = multi_texture_create();
	if (texture == NULL) {
		return NULL;
	}
	texture->width = width;
	texture->height = height;

	struct wlr_multi_renderer_child *child;
	wl_list_for_each(child, &renderer->children, link) {
		struct wlr_texture *wlr_texture_child = wlr_texture_from_pixels(
			child->renderer, fmt, stride, width, height, data);
		if (wlr_texture_child == NULL) {
			wlr_texture_destroy(&texture->texture);
			return NULL;
		}

		multi_texture_add(texture, wlr_texture_child, child->renderer);
	}

	return &texture->texture;
}

static void renderer_destroy(struct wlr_renderer *wlr_renderer) {
	struct wlr_multi_renderer *renderer = renderer_get_multi(wlr_renderer);

	struct wlr_multi_renderer_child *child, *tmp;
	wl_list_for_each_safe(child, tmp, &renderer->children, link) {
		wlr_renderer_destroy(child->renderer);
	}

	free(renderer->formats);
	free(renderer);
}

static const struct wlr_renderer_impl renderer_impl = {
	.begin = renderer_attempt_render,
	.clear = renderer_attempt_render,
	.scissor = renderer_attempt_render,
	.render_texture_with_matrix = renderer_attempt_render_texture,
	.render_quad_with_matrix = renderer_attempt_render,
	.render_ellipse_with_matrix = renderer_attempt_render,
	.get_formats = renderer_get_formats,
	.format_supported = renderer_format_supported,
	.texture_from_pixels = renderer_texture_from_pixels,
	.destroy = renderer_destroy,
};

struct wlr_renderer *wlr_multi_renderer_create() {
	struct wlr_multi_renderer *renderer =
		calloc(1, sizeof(struct wlr_multi_renderer));
	if (renderer == NULL) {
		return NULL;
	}
	wl_list_init(&renderer->children);
	wlr_renderer_init(&renderer->renderer, &renderer_impl);
	return &renderer->renderer;
}

static void renderer_child_destroy(struct wlr_multi_renderer_child *child) {
	wl_list_remove(&child->destroy.link);
	wl_list_remove(&child->link);
	multi_renderer_update_formats(child->parent);
	free(child);
}

static void multi_renderer_child_handle_destroy(struct wl_listener *listener,
		void *data) {
	struct wlr_multi_renderer_child *child =
		wl_container_of(listener, child, destroy);
	renderer_child_destroy(child);
}

void wlr_multi_renderer_add(struct wlr_renderer *wlr_renderer,
		struct wlr_renderer *wlr_child) {
	struct wlr_multi_renderer *renderer = renderer_get_multi(wlr_renderer);

	// Check if it's already added
	struct wlr_multi_renderer_child *child;
	wl_list_for_each(child, &renderer->children, link) {
		if (child->renderer == wlr_child) {
			return;
		}
	}

	child = calloc(1, sizeof(struct wlr_multi_renderer_child));
	if (child == NULL) {
		wlr_log(L_ERROR, "Allocation failed");
		return;
	}
	child->renderer = wlr_child;
	child->parent = renderer;

	wl_list_insert(&renderer->children, &child->link);

	wl_signal_add(&wlr_child->events.destroy, &child->destroy);
	child->destroy.notify = multi_renderer_child_handle_destroy;

	multi_renderer_update_formats(renderer);
}

void wlr_multi_renderer_remove(struct wlr_renderer *wlr_renderer,
		struct wlr_renderer *wlr_child) {
	struct wlr_multi_renderer *renderer = renderer_get_multi(wlr_renderer);

	struct wlr_multi_renderer_child *child, *tmp;
	wl_list_for_each_safe(child, tmp, &renderer->children, link) {
		if (child->renderer == wlr_child) {
			renderer_child_destroy(child);
			break;
		}
	}
}

bool wlr_multi_renderer_is_empty(struct wlr_renderer *wlr_renderer) {
	struct wlr_multi_renderer *renderer = renderer_get_multi(wlr_renderer);
	return wl_list_empty(&renderer->children);
}

bool wlr_renderer_is_multi(struct wlr_renderer *wlr_renderer) {
	return wlr_renderer->impl == &renderer_impl;
}
