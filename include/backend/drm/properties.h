#ifndef BACKEND_DRM_PROPERTIES_H
#define BACKEND_DRM_PROPERTIES_H

#include <stdbool.h>
#include <stdint.h>

/*
 * These types contain the property ids for several DRM objects.
 * See https://01.org/linuxgraphics/gfx-docs/drm/gpu/drm-kms.html#kms-properties
 * for more details.
 */

union wlr_drm_connector_props {
	struct {
		uint32_t edid;
		uint32_t dpms;
		uint32_t link_status; // not guaranteed to exist
		uint32_t path;
		uint32_t non_desktop;

		// atomic-modesetting only

		uint32_t crtc_id;
	};
	uint32_t props[4];
};

union wlr_drm_crtc_props {
	struct {
		// Neither of these are guaranteed to exist
		uint32_t rotation;
		uint32_t scaling_mode;

		// atomic-modesetting only

		uint32_t active;
		uint32_t mode_id;
		uint32_t gamma_lut;
		uint32_t gamma_lut_size;
	};
	uint32_t props[6];
};

union wlr_drm_plane_props {
	struct {
		uint32_t type;
		uint32_t rotation; // Not guaranteed to exist
		uint32_t in_formats; // Not guaranteed to exist

		// atomic-modesetting only

		uint32_t src_x;
		uint32_t src_y;
		uint32_t src_w;
		uint32_t src_h;
		uint32_t crtc_x;
		uint32_t crtc_y;
		uint32_t crtc_w;
		uint32_t crtc_h;
		uint32_t fb_id;
		uint32_t crtc_id;
	};
	uint32_t props[13];
};

bool get_drm_connector_props(int fd, uint32_t id,
	union wlr_drm_connector_props *out);
bool get_drm_crtc_props(int fd, uint32_t id, union wlr_drm_crtc_props *out);
bool get_drm_plane_props(int fd, uint32_t id, union wlr_drm_plane_props *out);

bool get_drm_prop(int fd, uint32_t obj, uint32_t prop, uint64_t *ret);
void *get_drm_prop_blob(int fd, uint32_t obj, uint32_t prop, size_t *ret_len);

#endif
