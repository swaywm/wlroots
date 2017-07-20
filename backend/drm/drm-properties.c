#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <wlr/util/log.h>
#include "drm-properties.h"

/*
 * Creates a mapping between property names and an array index where to store
 * the ids.  The prop_info arrays must be sorted by name, as bsearch is used to
 * search them.
 */
struct prop_info {
	const char *name;
	size_t index;
};

static const struct prop_info connector_info[] = {
#define INDEX(name) (offsetof(union wlr_drm_connector_props, name) / sizeof(uint32_t))
	{ "CRTC_ID", INDEX(crtc_id) },
	{ "DPMS",    INDEX(dpms) },
	{ "EDID",    INDEX(edid) },
#undef INDEX
};

static const struct prop_info crtc_info[] = {
#define INDEX(name) (offsetof(union wlr_drm_crtc_props, name) / sizeof(uint32_t))
	{ "rotation",  INDEX(rotation) },
	{ "scaling mode", INDEX(scaling_mode) },
#undef INDEX
};

static const struct prop_info plane_info[] = {
#define INDEX(name) (offsetof(union wlr_drm_plane_props, name) / sizeof(uint32_t))
	{ "CRTC_H",  INDEX(crtc_h) },
	{ "CRTC_ID", INDEX(crtc_id) },
	{ "CRTC_W",  INDEX(crtc_w) },
	{ "CRTC_X",  INDEX(crtc_x) },
	{ "CRTC_Y",  INDEX(crtc_y) },
	{ "FB_ID",   INDEX(fb_id) },
	{ "SRC_H",   INDEX(src_h) },
	{ "SRC_W",   INDEX(src_w) },
	{ "SRC_X",   INDEX(src_x) },
	{ "SRC_Y",   INDEX(src_y) },
	{ "type",    INDEX(type) },
#undef INDEX
};

static int cmp_prop_info(const void *arg1, const void *arg2) {
	const struct prop_info *a = arg1;
	const struct prop_info *b = arg2;

	return strcmp(a->name, b->name);
}

static bool scan_properties(int fd, uint32_t id, uint32_t type, uint32_t *result,
		const struct prop_info *info, size_t info_len) {
	drmModeObjectProperties *props = drmModeObjectGetProperties(fd, id, type);
	if (!props) {
		wlr_log_errno(L_ERROR, "Failed to get DRM object properties");
		return false;
	}

	for (uint32_t i = 0; i < props->count_props; ++i) {
		drmModePropertyRes *prop = drmModeGetProperty(fd, props->props[i]);
		if (!prop) {
			wlr_log_errno(L_ERROR, "Failed to get DRM object property");
			continue;
		}

		const struct prop_info *p =
			bsearch(prop->name, info, info_len, sizeof(info[0]), cmp_prop_info);
		if (p) {
			result[p->index] = prop->prop_id;
		}

		drmModeFreeProperty(prop);
	}

	drmModeFreeObjectProperties(props);
	return true;
}

bool wlr_drm_get_connector_props(int fd, uint32_t id, union wlr_drm_connector_props *out) {
	return scan_properties(fd, id, DRM_MODE_OBJECT_CONNECTOR, out->props,
		connector_info, sizeof(connector_info) / sizeof(connector_info[0]));
}

bool wlr_drm_get_crtc_props(int fd, uint32_t id, union wlr_drm_crtc_props *out) {
	return scan_properties(fd, id, DRM_MODE_OBJECT_CRTC, out->props,
		crtc_info, sizeof(crtc_info) / sizeof(crtc_info[0]));
}

bool wlr_drm_get_plane_props(int fd, uint32_t id, union wlr_drm_plane_props *out) {
	return scan_properties(fd, id, DRM_MODE_OBJECT_PLANE, out->props,
		plane_info, sizeof(plane_info) / sizeof(plane_info[0]));
}
