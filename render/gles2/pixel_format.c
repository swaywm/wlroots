#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include "render/gles2.h"

/*
* The wayland formats are little endian while the GL formats are big endian,
* so WL_SHM_FORMAT_ARGB8888 is actually compatible with GL_BGRA_EXT.
*/
struct gles2_pixel_format formats[] = {
	{
		.wl_format = WL_SHM_FORMAT_ARGB8888,
		.depth = 32,
		.bpp = 32,
		.gl_format = GL_BGRA_EXT,
		.gl_type = GL_UNSIGNED_BYTE,
	},
	{
		.wl_format = WL_SHM_FORMAT_XRGB8888,
		.depth = 24,
		.bpp = 32,
		.gl_format = GL_BGRA_EXT,
		.gl_type = GL_UNSIGNED_BYTE,
	},
	{
		.wl_format = WL_SHM_FORMAT_XBGR8888,
		.depth = 24,
		.bpp = 32,
		.gl_format = GL_RGBA,
		.gl_type = GL_UNSIGNED_BYTE,
	},
	{
		.wl_format = WL_SHM_FORMAT_ABGR8888,
		.depth = 32,
		.bpp = 32,
		.gl_format = GL_RGBA,
		.gl_type = GL_UNSIGNED_BYTE,
	},
};
// TODO: more pixel formats

const struct gles2_pixel_format *gles2_format_from_wl(enum wl_shm_format fmt) {
	for (size_t i = 0; i < sizeof(formats) / sizeof(*formats); ++i) {
		if (formats[i].wl_format == fmt) {
			return &formats[i];
		}
	}
	return NULL;
}
