#include <drm_fourcc.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include "render/gles2.h"

/*
 * The DRM formats are little endian while the GL formats are big endian,
 * so DRM_FORMAT_ARGB8888 is actually compatible with GL_BGRA_EXT.
 */
static const struct wlr_gles2_pixel_format formats[] = {
	{
		.drm_format = DRM_FORMAT_ARGB8888,
		.gl_format = GL_BGRA_EXT,
		.gl_type = GL_UNSIGNED_BYTE,
		.has_alpha = true,
	},
	{
		.drm_format = DRM_FORMAT_XRGB8888,
		.gl_format = GL_BGRA_EXT,
		.gl_type = GL_UNSIGNED_BYTE,
		.has_alpha = false,
	},
	{
		.drm_format = DRM_FORMAT_XBGR8888,
		.gl_format = GL_RGBA,
		.gl_type = GL_UNSIGNED_BYTE,
		.has_alpha = false,
	},
	{
		.drm_format = DRM_FORMAT_ABGR8888,
		.gl_format = GL_RGBA,
		.gl_type = GL_UNSIGNED_BYTE,
		.has_alpha = true,
	},
	{
		.drm_format = DRM_FORMAT_BGR888,
		.gl_format = GL_RGB,
		.gl_type = GL_UNSIGNED_BYTE,
		.has_alpha = false,
	},
#if WLR_LITTLE_ENDIAN
	{
		.drm_format = DRM_FORMAT_RGBX4444,
		.gl_format = GL_RGBA,
		.gl_type = GL_UNSIGNED_SHORT_4_4_4_4,
		.has_alpha = false,
	},
	{
		.drm_format = DRM_FORMAT_RGBA4444,
		.gl_format = GL_RGBA,
		.gl_type = GL_UNSIGNED_SHORT_4_4_4_4,
		.has_alpha = true,
	},
	{
		.drm_format = DRM_FORMAT_RGBX5551,
		.gl_format = GL_RGBA,
		.gl_type = GL_UNSIGNED_SHORT_5_5_5_1,
		.has_alpha = false,
	},
	{
		.drm_format = DRM_FORMAT_RGBA5551,
		.gl_format = GL_RGBA,
		.gl_type = GL_UNSIGNED_SHORT_5_5_5_1,
		.has_alpha = true,
	},
	{
		.drm_format = DRM_FORMAT_RGB565,
		.gl_format = GL_RGB,
		.gl_type = GL_UNSIGNED_SHORT_5_6_5,
		.has_alpha = false,
	},
	{
		.drm_format = DRM_FORMAT_XBGR2101010,
		.gl_format = GL_RGBA,
		.gl_type = GL_UNSIGNED_INT_2_10_10_10_REV_EXT,
		.has_alpha = false,
	},
	{
		.drm_format = DRM_FORMAT_ABGR2101010,
		.gl_format = GL_RGBA,
		.gl_type = GL_UNSIGNED_INT_2_10_10_10_REV_EXT,
		.has_alpha = true,
	},
	{
		.drm_format = DRM_FORMAT_XBGR16161616F,
		.gl_format = GL_RGBA,
		.gl_type = GL_HALF_FLOAT_OES,
		.has_alpha = false,
	},
	{
		.drm_format = DRM_FORMAT_ABGR16161616F,
		.gl_format = GL_RGBA,
		.gl_type = GL_HALF_FLOAT_OES,
		.has_alpha = true,
	},
#endif
};

// TODO: more pixel formats

bool is_gles2_pixel_format_supported(const struct wlr_gles2_renderer *renderer,
		const struct wlr_gles2_pixel_format *format) {
	if (format->gl_type == GL_UNSIGNED_INT_2_10_10_10_REV_EXT
			&& !renderer->exts.EXT_texture_type_2_10_10_10_REV) {
		return false;
	}
	if (format->gl_type == GL_HALF_FLOAT_OES
			&& !renderer->exts.OES_texture_half_float_linear) {
		return false;
	}
	if (format->gl_format == GL_BGRA_EXT
			&& !renderer->exts.EXT_read_format_bgra) {
		return false;
	}
	return true;
}

const struct wlr_gles2_pixel_format *get_gles2_format_from_drm(uint32_t fmt) {
	for (size_t i = 0; i < sizeof(formats) / sizeof(*formats); ++i) {
		if (formats[i].drm_format == fmt) {
			return &formats[i];
		}
	}
	return NULL;
}

const struct wlr_gles2_pixel_format *get_gles2_format_from_gl(
		GLint gl_format, GLint gl_type, bool alpha) {
	for (size_t i = 0; i < sizeof(formats) / sizeof(*formats); ++i) {
		if (formats[i].gl_format == gl_format &&
				formats[i].gl_type == gl_type &&
				formats[i].has_alpha == alpha) {
			return &formats[i];
		}
	}
	return NULL;
}

void init_gles2_data_ptr_formats(struct wlr_gles2_renderer *renderer) {
	for (size_t i = 0; i < sizeof(formats) / sizeof(formats[0]); i++) {
		if (!is_gles2_pixel_format_supported(renderer, &formats[i])) {
			continue;
		}
		wlr_drm_format_set_add(&renderer->data_ptr_texture_formats,
			formats[i].drm_format, DRM_FORMAT_MOD_LINEAR);
	}
}
