#include <assert.h>
#include <stdlib.h>

#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <drm_fourcc.h>
#include <gbm.h>
#include <pixman.h>
#include <wayland-server.h>

#include <wlr/render/egl.h>
#include <wlr/render/renderer.h>
#include <wlr/render/renderer/interface.h>
#include <wlr/render/shm.h>
#include <wlr/util/log.h>

#include "render/renderer/gles.h"

static const struct wlr_texture_impl_2 gles_texture_impl;

static struct wlr_gles_texture *wlr_gles_texture(struct wlr_texture_2 *base) {
	assert(base->impl == &gles_texture_impl);
	return (struct wlr_gles_texture *)base;
}

static void gles_texture_nullify(struct wlr_gles_texture *tex) {
	tex->type = WLR_GLES_TEXTURE_NULL;
	if (tex->texture) {
		glDeleteTextures(1, &tex->texture);
		tex->texture = 0;
	}
	if (tex->egl) {
		wlr_egl_destroy_image_2(tex->gles->egl, tex->egl);
		tex->egl = NULL;
	}
	if (tex->img.bo) {
		gbm_bo_destroy(tex->img.bo);
		tex->img.bo = NULL;
		tex->img.base.width = 0;
		tex->img.base.height = 0;
		tex->img.base.format = 0;
		tex->img.base.modifier = 0;
	}
	tex->fmt = NULL;
	tex->resource = NULL;
}

static void gles_texture_destroy(struct wlr_texture_2 *base) {
	struct wlr_gles_texture *tex = wlr_gles_texture(base);

	gles_texture_nullify(tex);
	free(tex);
}

static void gles_shm_write_subimage(void *userdata, const void *data,
		uint32_t stride, const pixman_rectangle32_t *rect) {
	struct wlr_gles_texture *tex = userdata;

	glPixelStorei(GL_UNPACK_ROW_LENGTH_EXT, stride);
	glPixelStorei(GL_UNPACK_SKIP_PIXELS_EXT, rect->x);
	glPixelStorei(GL_UNPACK_SKIP_ROWS_EXT, rect->y);

	glTexSubImage2D(GL_TEXTURE_2D, 0, rect->x, rect->y, rect->width,
		rect->height, tex->fmt->format, tex->fmt->type, data);

	glPixelStorei(GL_UNPACK_ROW_LENGTH_EXT, 0);
	glPixelStorei(GL_UNPACK_SKIP_PIXELS_EXT, 0);
	glPixelStorei(GL_UNPACK_SKIP_ROWS_EXT, 0);
}

static void gles_shm_write_fallback(void *userdata, const void *data_arg,
		uint32_t stride, const pixman_rectangle32_t *rect) {
	struct wlr_gles_texture *tex = userdata;
	const uint8_t (*data)[stride] = data_arg;

	for (uint32_t y = rect->y; y < rect->y + rect->height; ++y) {
		glTexSubImage2D(GL_TEXTURE_2D, 0, rect->x, y, rect->width, 1,
			tex->fmt->format, tex->fmt->type, data[y]);
	}
}

static bool gles_texture_shm(struct wlr_gles_texture *tex,
		struct wl_shm_buffer *shm, pixman_region32_t *damage) {
	struct wlr_gles *gles = tex->gles;
	uint32_t width = wl_shm_buffer_get_width(shm);
	uint32_t height = wl_shm_buffer_get_height(shm);
	uint32_t format = wl_shm_buffer_get_format(shm);

	/*
	 * WL_SHM_FORMAT_* and DRM_FORMAT_* are compatible,
	 * except for these two values.
	 */
	if (format == WL_SHM_FORMAT_ARGB8888) {
		format = DRM_FORMAT_ARGB8888;
	} else if (format == WL_SHM_FORMAT_XRGB8888) {
		format = DRM_FORMAT_XRGB8888;
	}

	if (tex->type != WLR_GLES_TEXTURE_SHM ||
			width != gbm_bo_get_width(tex->img.bo) ||
			height != gbm_bo_get_height(tex->img.bo) ||
			format != gbm_bo_get_format(tex->img.bo)) {
		gles_texture_nullify(tex);
	}

	if (tex->type == WLR_GLES_TEXTURE_NULL) {
		tex->type = WLR_GLES_TEXTURE_SHM;
		tex->fmt = gles_rgb_format_from_fourcc(gles, format);
		if (!tex->fmt) {
			goto error_nullify;
		}

		// XXX: Should we be doing anything with modifiers
		// here?
		tex->img.bo = gbm_bo_create(gles->gbm->gbm,
			width, height, format,
			GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
		if (!tex->img.bo) {
			wlr_log_errno(WLR_ERROR, "Failed to create buffer");
			goto error_nullify;
		}

		tex->egl = wlr_egl_create_image_2(gles->egl, tex->img.bo);
		if (!tex->egl) {
			wlr_log_errno(WLR_ERROR, "Failed to create EGL image");
			goto error_nullify;
		}

		glGenTextures(1, &tex->texture);
		glBindTexture(GL_TEXTURE_2D, tex->texture);
		gles->egl_image_target_texture_2d(GL_TEXTURE_2D, tex->egl);

		tex->img.base.width = gbm_bo_get_width(tex->img.bo);
		tex->img.base.height = gbm_bo_get_height(tex->img.bo);
		tex->img.base.format = gbm_bo_get_format(tex->img.bo);
		tex->img.base.modifier = gbm_bo_get_modifier(tex->img.bo);

		// Make sure we read the entirety of the source buffer
		damage = NULL;
	}

	glBindTexture(GL_TEXTURE_2D, tex->texture);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

	if (tex->gles->has_unpack_subimage) {
		wlr_shm_apply_damage(shm, damage, tex, gles_shm_write_subimage);
	} else {
		wlr_shm_apply_damage(shm, damage, tex, gles_shm_write_fallback);
	}

	glBindTexture(GL_TEXTURE_2D, 0);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 0);

	return true;

error_nullify:
	gles_texture_nullify(tex);
	return false;
}

static bool gles_texture_apply_damage(struct wlr_texture_2 *base,
		struct wl_resource *buffer, pixman_region32_t *damage) {
	struct wlr_gles_texture *tex = wlr_gles_texture(base);

	if (tex->type == WLR_GLES_TEXTURE_PIXELS) {
		wlr_log(WLR_ERROR, "Invalid texture type");
		return false;
	}

	if (!buffer) {
		gles_texture_nullify(tex);
		return true;
	}

	struct wl_shm_buffer *shm = wl_shm_buffer_get(buffer);
	if (shm) {
		return gles_texture_shm(tex, shm, damage);
	}

	return false;
#if 0
	gles_texture_nullify(tex);

	struct gbm_bo *bo = gbm_bo_import(tex->gles->gbm->gbm,
		GBM_BO_IMPORT_WL_BUFFER, buffer, GBM_BO_USE_SCANOUT);
	if (bo) {
		tex->type = WLR_GLES_TEXTURE_EGL;
	}

	return true;
#endif
}

static const struct wlr_texture_impl_2 gles_texture_impl = {
	.destroy = gles_texture_destroy,
	.apply_damage = gles_texture_apply_damage,
};

struct wlr_gles_texture *gles_texture_create(struct wlr_gles *gles) {
	struct wlr_gles_texture *tex = calloc(1, sizeof(*tex));
	if (!tex) {
		wlr_log_errno(WLR_ERROR, "Allocation failed");
		return NULL;
	}

	tex->gles = gles;
	tex->type = WLR_GLES_TEXTURE_NULL;
	wl_signal_init(&tex->img.base.release);

	wlr_egl_make_current_2(gles->egl);

	wlr_texture_init_2(&tex->base, &gles_texture_impl);
	return tex;
}
