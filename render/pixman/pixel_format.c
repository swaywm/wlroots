#include <drm_fourcc.h>
#include <wlr/util/log.h>

#include "render/pixman.h"

static const struct wlr_pixman_pixel_format formats[] = {
	{
		.drm_format = DRM_FORMAT_ARGB8888,
#if WLR_BIG_ENDIAN
		.pixman_format = PIXMAN_b8g8r8a8,
#else
		.pixman_format = PIXMAN_a8r8g8b8,
#endif
	},
	{
		.drm_format = DRM_FORMAT_XBGR8888,
#if WLR_BIG_ENDIAN
		.pixman_format = PIXMAN_r8g8b8x8,
#else
		.pixman_format = PIXMAN_x8b8g8r8,
#endif
	},
	{
		.drm_format = DRM_FORMAT_XRGB8888,
#if WLR_BIG_ENDIAN
		.pixman_format = PIXMAN_b8g8r8x8,
#else
		.pixman_format = PIXMAN_x8r8g8b8,
#endif
	},
	{
		.drm_format = DRM_FORMAT_ABGR8888,
#if WLR_BIG_ENDIAN
		.pixman_format = PIXMAN_r8g8b8a8,
#else
		.pixman_format = PIXMAN_a8b8g8r8,
#endif
	}
};

pixman_format_code_t get_pixman_format_from_drm(uint32_t fmt) {
	for (size_t i = 0; i < sizeof(formats) / sizeof(*formats); ++i) {
		if (formats[i].drm_format == fmt) {
			return formats[i].pixman_format;
		}
	}

	wlr_log(WLR_ERROR, "DRM format 0x%"PRIX32" has no pixman equivalent", fmt);
	return 0;
}

const uint32_t *get_pixman_drm_formats(size_t *len) {
	static uint32_t drm_formats[sizeof(formats) / sizeof(formats[0])];
	*len = sizeof(formats) / sizeof(formats[0]);
	for (size_t i = 0; i < sizeof(formats) / sizeof(formats[0]); i++) {
		drm_formats[i] = formats[i].drm_format;
	}
	return drm_formats;
}

void fill_pixman_render_drm_formats(struct wlr_drm_format_set *out) {
	for (size_t i = 0; i < sizeof(formats) / sizeof(formats[0]); i++) {
		if (pixman_format_supported_destination(formats[i].pixman_format)) {
			wlr_drm_format_set_add(out, formats[i].drm_format,
				DRM_FORMAT_MOD_LINEAR);
		}
	}
}
