#ifndef WLR_RENDER_DRM_FORMAT_SET_H
#define WLR_RENDER_DRM_FORMAT_SET_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct wlr_drm_format {
	uint32_t format;
	size_t len, cap;
	uint64_t modifiers[];
};

struct wlr_drm_format_set {
	size_t len, cap;
	struct wlr_drm_format **formats;
};

void wlr_drm_format_set_finish(struct wlr_drm_format_set *set);

const struct wlr_drm_format *wlr_drm_format_set_get(
	const struct wlr_drm_format_set *set, uint32_t format);

bool wlr_drm_format_set_has(const struct wlr_drm_format_set *set,
	uint32_t format, uint64_t modifier);

bool wlr_drm_format_set_add(struct wlr_drm_format_set *set, uint32_t format,
	uint64_t modifier);

#endif
