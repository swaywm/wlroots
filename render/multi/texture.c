#include <assert.h>
#include <stdlib.h>
#include <wlr/render/interface.h>
#include <wlr/util/log.h>
#include "render/multi.h"

static const struct wlr_texture_impl texture_impl;

bool wlr_texture_is_multi(struct wlr_texture *wlr_texture) {
	return wlr_texture->impl == &texture_impl;
}

static struct wlr_multi_texture *texture_get_multi(
		struct wlr_texture *wlr_texture) {
	assert(wlr_texture_is_multi(wlr_texture));
	return (struct wlr_multi_texture *)wlr_texture;
}

static void texture_get_size(struct wlr_texture *wlr_texture, int *width,
		int *height) {
	struct wlr_multi_texture *texture = texture_get_multi(wlr_texture);
	*width = texture->width;
	*height = texture->height;
}

static bool texture_write_pixels(struct wlr_texture *wlr_texture,
		enum wl_shm_format fmt, uint32_t stride, uint32_t width,
		uint32_t height, uint32_t src_x, uint32_t src_y, uint32_t dst_x,
		uint32_t dst_y, const void *data) {
	struct wlr_multi_texture *texture = texture_get_multi(wlr_texture);

	struct wlr_multi_texture_child *child;
	wl_list_for_each(child, &texture->children, link) {
		if (!wlr_texture_write_pixels(child->texture, fmt, stride, width,
				height, src_x, src_y, dst_x, dst_y, data)) {
			return false;
		}
	}

	return true;
}

static void texture_child_destroy(struct wlr_multi_texture_child *child) {
	wl_list_remove(&child->link);
	free(child);
}

static void texture_destroy(struct wlr_texture *wlr_texture) {
	struct wlr_multi_texture *texture = texture_get_multi(wlr_texture);

	struct wlr_multi_texture_child *child, *tmp;
	wl_list_for_each_safe(child, tmp, &texture->children, link) {
		wlr_texture_destroy(child->texture);
		texture_child_destroy(child);
	}

	free(texture);
}

static const struct wlr_texture_impl texture_impl = {
	.get_size = texture_get_size,
	.write_pixels = texture_write_pixels,
	.destroy = texture_destroy,
};

struct wlr_multi_texture *multi_texture_create() {
	struct wlr_multi_texture *texture =
		calloc(1, sizeof(struct wlr_multi_texture));
	if (texture == NULL) {
		return NULL;
	}
	wlr_texture_init(&texture->texture, &texture_impl);
	wl_list_init(&texture->children);
	return texture;
}

void multi_texture_add(struct wlr_multi_texture *texture,
		struct wlr_texture *wlr_child, struct wlr_renderer *child_renderer) {
	struct wlr_multi_texture_child *child =
		calloc(1, sizeof(struct wlr_multi_texture_child));
	if (child == NULL) {
		wlr_log(L_ERROR, "Allocation failed");
		return;
	}
	child->texture = wlr_child;
	child->renderer = child_renderer;

	wl_list_insert(&texture->children, &child->link);
}

struct wlr_texture *wlr_multi_texture_get_child(struct wlr_texture *wlr_texture,
		struct wlr_renderer *child_renderer) {
	struct wlr_multi_texture *texture = texture_get_multi(wlr_texture);

	struct wlr_multi_texture_child *child;
	wl_list_for_each(child, &texture->children, link) {
		if (child->renderer == child_renderer) {
			return child->texture;
		}
	}

	return NULL;
}

bool wlr_multi_texture_is_empty(struct wlr_texture *wlr_texture) {
	struct wlr_multi_texture *texture = texture_get_multi(wlr_texture);
	return wl_list_empty(&texture->children);
}
