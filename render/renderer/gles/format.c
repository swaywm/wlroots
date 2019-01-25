#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <drm_fourcc.h>

#include "render/renderer/gles.h"

/*
 * OpenGL annoyingly uses system endianess (except where it doesn't)
 * so some of these formats are only valid on little endian.
 *
 * See https://afrantzis.com/pixel-format-guide/ for an explination.
 */

static const struct wlr_gles_rgb_format rgb[] = {
	/* 32 bpp formats */
	{
		.fourcc = DRM_FORMAT_ARGB8888,
		.format = GL_BGRA_EXT,
		.type = GL_UNSIGNED_BYTE,
	},
	{
		.fourcc = DRM_FORMAT_XRGB8888,
		.format = GL_BGRA_EXT,
		.type = GL_UNSIGNED_BYTE,
		.opaque = true,
	},
	{
		.fourcc = DRM_FORMAT_ABGR8888,
		.format = GL_RGBA,
		.type = GL_UNSIGNED_BYTE,
	},
	{
		.fourcc = DRM_FORMAT_XBGR8888,
		.format = GL_RGBA,
		.type = GL_UNSIGNED_BYTE,
		.opaque = true,
	},
#if WLR_LITTLE_ENDIAN
	{
		.fourcc = DRM_FORMAT_ABGR2101010,
		.format = GL_RGBA,
		.type = GL_UNSIGNED_INT_2_10_10_10_REV_EXT,
		.is_10bit = true,
	},
	{
		.fourcc = DRM_FORMAT_XBGR2101010,
		.format = GL_RGBA,
		.type = GL_UNSIGNED_INT_2_10_10_10_REV_EXT,
		.is_10bit = true,
		.opaque = true,
	},
#endif
	/* 24 bpp formats */
	{
		.fourcc = DRM_FORMAT_BGR888,
		.format = GL_RGB,
		.type = GL_UNSIGNED_BYTE,
	},
#if WLR_LITTLE_ENDIAN
	/* 16 bpp formats */
	{
		// Little
		.fourcc = DRM_FORMAT_RGBA4444,
		.format = GL_RGBA,
		.type = GL_UNSIGNED_SHORT_4_4_4_4,
	},
	{
		// Little
		.fourcc = DRM_FORMAT_RGBA5551,
		.format = GL_RGBA,
		.type = GL_UNSIGNED_SHORT_5_5_5_1,
	},
	{
		// Little
		.fourcc = DRM_FORMAT_RGB565,
		.format = GL_RGB,
		.type = GL_UNSIGNED_SHORT_5_6_5,
	},
#endif
};

const struct wlr_gles_rgb_format *gles_rgb_format_from_fourcc(
		struct wlr_gles *gles, uint32_t fourcc) {
	for (size_t i = 0; i < sizeof(rgb) / sizeof(rgb[0]); ++i) {
		const struct wlr_gles_rgb_format *fmt = &rgb[i];

		if (fmt->fourcc == fourcc) {
			if (fmt->is_10bit && !gles->has_texture_type_2_10_10_10_rev) {
				return NULL;
			}
			return fmt;
		}
	}
	return NULL;
}

const struct wlr_gles_rgb_format *gles_rgb_format_from_gl(
		struct wlr_gles *gles, GLenum format, GLenum type) {
	for (size_t i = 0; i < sizeof(rgb) / sizeof(rgb[0]); ++i) {
		const struct wlr_gles_rgb_format *fmt = &rgb[i];

		if (fmt->format == format && fmt->type == type) {
			if (fmt->is_10bit && !gles->has_texture_type_2_10_10_10_rev) {
				return NULL;
			}
			return fmt;
		}
	}
	return NULL;
}

bool gles_populate_shm_formats(struct wlr_gles *gles) {
	for (size_t i = 0; i < sizeof(rgb) / sizeof(rgb[0]); ++i) {
		const struct wlr_gles_rgb_format *fmt = &rgb[i];

		if (fmt->is_10bit && !gles->has_texture_type_2_10_10_10_rev) {
			continue;
		}

		if (!wlr_format_set_add(&gles->shm_formats, fmt->fourcc,
				DRM_FORMAT_MOD_LINEAR)) {
			return false;
		}
	}
	return true;
}
