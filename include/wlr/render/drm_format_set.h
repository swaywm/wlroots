#ifndef WLR_RENDER_DRM_FORMAT_SET_H
#define WLR_RENDER_DRM_FORMAT_SET_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** A single DRM format */
struct wlr_drm_format {
	// The actual DRM format, from `drm_fourcc.h`
	uint32_t format;
	// The number of modifiers
	size_t len;
	// The capacity of the array; do not use.
	size_t capacity;
	// The actual modifiers
	uint64_t modifiers[];
};

/** A set of DRM formats */
struct wlr_drm_format_set {
	// The number of formats
	size_t len;
	// The capacity of the array; private to wlroots
	size_t capacity;
	// A pointer to an array of `struct wlr_drm_format *` of length `len`.
	struct wlr_drm_format **formats;
};

/**
 * Free all of the DRM formats in the set, making the set empty.  Does not
 * free the set itself.
 */
void wlr_drm_format_set_finish(struct wlr_drm_format_set *set);

/**
 * Return a pointer to a member of this `wlr_drm_format_set` of format
 * `format`, or NULL if none exists.
 */
const struct wlr_drm_format *wlr_drm_format_set_get(
	const struct wlr_drm_format_set *set, uint32_t format);

bool wlr_drm_format_set_has(const struct wlr_drm_format_set *set,
	uint32_t format, uint64_t modifier);

bool wlr_drm_format_set_add(struct wlr_drm_format_set *set, uint32_t format,
	uint64_t modifier);

#endif
