#ifndef RENDER_DRM_FORMAT_SET_H
#define RENDER_DRM_FORMAT_SET_H

#include <wlr/render/drm_format_set.h>

struct wlr_drm_format *wlr_drm_format_dup(const struct wlr_drm_format *format);
/**
 * Intersect modifiers for two DRM formats.
 *
 * Both arguments must have the same format field. If the formats aren't
 * compatible, NULL is returned. If either format doesn't support any modifier,
 * a format that doesn't support any modifier is returned.
 */
struct wlr_drm_format *wlr_drm_format_intersect(
	const struct wlr_drm_format *a, const struct wlr_drm_format *b);

#endif
