#include <drm_fourcc.h>
#include "render/pixel_format.h"

static const struct wlr_pixel_format_info pixel_format_info[] = {
	{
		.drm_format = DRM_FORMAT_XRGB8888,
		.opaque_substitute = DRM_FORMAT_INVALID,
		.bpp = 32,
		.has_alpha = false,
	},
	{
		.drm_format = DRM_FORMAT_ARGB8888,
		.opaque_substitute = DRM_FORMAT_XRGB8888,
		.bpp = 32,
		.has_alpha = true,
	}
};

static const size_t pixel_format_info_size =
	sizeof(pixel_format_info) / sizeof(pixel_format_info[0]);

const struct wlr_pixel_format_info *drm_get_pixel_format_info(uint32_t fmt) {
	for (size_t i = 0; i < pixel_format_info_size; ++i) {
		if (pixel_format_info[i].drm_format == fmt) {
			return &pixel_format_info[i];
		}
	}

	return NULL;
}
