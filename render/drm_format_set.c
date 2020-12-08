#include <assert.h>
#include <drm_fourcc.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wlr/render/drm_format_set.h>
#include <wlr/util/log.h>
#include "render/drm_format_set.h"

void wlr_drm_format_set_finish(struct wlr_drm_format_set *set) {
	for (size_t i = 0; i < set->len; ++i) {
		free(set->formats[i]);
	}
	free(set->formats);

	set->len = 0;
	set->cap = 0;
	set->formats = NULL;
}

static struct wlr_drm_format **format_set_get_ref(struct wlr_drm_format_set *set,
		uint32_t format) {
	for (size_t i = 0; i < set->len; ++i) {
		if (set->formats[i]->format == format) {
			return &set->formats[i];
		}
	}

	return NULL;
}

const struct wlr_drm_format *wlr_drm_format_set_get(
		const struct wlr_drm_format_set *set, uint32_t format) {
	struct wlr_drm_format **ptr =
		format_set_get_ref((struct wlr_drm_format_set *)set, format);
	return ptr ? *ptr : NULL;
}

bool wlr_drm_format_set_has(const struct wlr_drm_format_set *set,
		uint32_t format, uint64_t modifier) {
	const struct wlr_drm_format *fmt = wlr_drm_format_set_get(set, format);
	if (!fmt) {
		return false;
	}

	if (modifier == DRM_FORMAT_MOD_INVALID) {
		return true;
	}

	for (size_t i = 0; i < fmt->len; ++i) {
		if (fmt->modifiers[i] == modifier) {
			return true;
		}
	}

	return false;
}

bool wlr_drm_format_set_add(struct wlr_drm_format_set *set, uint32_t format,
		uint64_t modifier) {
	assert(format != DRM_FORMAT_INVALID);
	struct wlr_drm_format **ptr = format_set_get_ref(set, format);

	if (ptr) {
		struct wlr_drm_format *fmt = *ptr;

		if (modifier == DRM_FORMAT_MOD_INVALID) {
			return true;
		}

		for (size_t i = 0; i < fmt->len; ++i) {
			if (fmt->modifiers[i] == modifier) {
				return true;
			}
		}

		if (fmt->len == fmt->cap) {
			size_t cap = fmt->cap ? fmt->cap * 2 : 4;

			fmt = realloc(fmt, sizeof(*fmt) + sizeof(fmt->modifiers[0]) * cap);
			if (!fmt) {
				wlr_log_errno(WLR_ERROR, "Allocation failed");
				return false;
			}

			fmt->cap = cap;
			*ptr = fmt;
		}

		fmt->modifiers[fmt->len++] = modifier;
		return true;
	}

	size_t cap = modifier != DRM_FORMAT_MOD_INVALID ? 4 : 0;

	struct wlr_drm_format *fmt =
		calloc(1, sizeof(*fmt) + sizeof(fmt->modifiers[0]) * cap);
	if (!fmt) {
		wlr_log_errno(WLR_ERROR, "Allocation failed");
		return false;
	}

	fmt->format = format;
	if (cap) {
		fmt->cap = cap;
		fmt->len = 1;
		fmt->modifiers[0] = modifier;
	}

	if (set->len == set->cap) {
		size_t new = set->cap ? set->cap * 2 : 4;

		struct wlr_drm_format **tmp = realloc(set->formats,
			sizeof(*fmt) + sizeof(fmt->modifiers[0]) * new);
		if (!tmp) {
			wlr_log_errno(WLR_ERROR, "Allocation failed");
			free(fmt);
			return false;
		}

		set->cap = new;
		set->formats = tmp;
	}

	set->formats[set->len++] = fmt;
	return true;
}

struct wlr_drm_format *wlr_drm_format_dup(const struct wlr_drm_format *format) {
	assert(format->len <= format->cap);
	size_t format_size = sizeof(struct wlr_drm_format) +
		format->cap * sizeof(format->modifiers[0]);
	struct wlr_drm_format *duped_format = malloc(format_size);
	if (duped_format == NULL) {
		return NULL;
	}
	memcpy(duped_format, format, format_size);
	return duped_format;
}

struct wlr_drm_format *wlr_drm_format_intersect(
		const struct wlr_drm_format *a, const struct wlr_drm_format *b) {
	assert(a->format == b->format);

	size_t format_cap = a->len < b->len ? a->len : b->len;
	size_t format_size = sizeof(struct wlr_drm_format) +
		format_cap * sizeof(a->modifiers[0]);
	struct wlr_drm_format *format = calloc(1, format_size);
	if (format == NULL) {
		wlr_log_errno(WLR_ERROR, "Allocation failed");
		return NULL;
	}
	format->format = a->format;
	format->cap = format_cap;

	for (size_t i = 0; i < a->len; i++) {
		for (size_t j = 0; j < b->len; j++) {
			if (a->modifiers[i] == b->modifiers[j]) {
				assert(format->len < format->cap);
				format->modifiers[format->len] = a->modifiers[i];
				format->len++;
				break;
			}
		}
	}

	// If both formats support modifiers, but the intersection is empty, then
	// the formats aren't compatible with each other
	if (format->len == 0 && a->len > 0 && b->len > 0) {
		free(format);
		return NULL;
	}

	return format;
}
