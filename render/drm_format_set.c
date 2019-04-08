#include <drm_fourcc.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wlr/render/drm_format_set.h>
#include <wlr/util/log.h>

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

bool wlr_drm_format_set_add(struct wlr_drm_format_set *set, uint32_t format,
		uint64_t modifier) {
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
