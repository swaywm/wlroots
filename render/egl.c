#include <assert.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <stdlib.h>
#include <wlr/render/egl.h>
#include <wlr/util/log.h>
#include "glapi.h"

// Extension documentation
// https://www.khronos.org/registry/EGL/extensions/KHR/EGL_KHR_image_base.txt.
// https://cgit.freedesktop.org/mesa/mesa/tree/docs/specs/WL_bind_wayland_display.spec

const char *egl_error(void) {
	switch (eglGetError()) {
	case EGL_SUCCESS:
		return "Success";
	case EGL_NOT_INITIALIZED:
		return "Not initialized";
	case EGL_BAD_ACCESS:
		return "Bad access";
	case EGL_BAD_ALLOC:
		return "Bad alloc";
	case EGL_BAD_ATTRIBUTE:
		return "Bad attribute";
	case EGL_BAD_CONTEXT:
		return "Bad Context";
	case EGL_BAD_CONFIG:
		return "Bad Config";
	case EGL_BAD_CURRENT_SURFACE:
		return "Bad current surface";
	case EGL_BAD_DISPLAY:
		return "Bad display";
	case EGL_BAD_SURFACE:
		return "Bad surface";
	case EGL_BAD_MATCH:
		return "Bad match";
	case EGL_BAD_PARAMETER:
		return "Bad parameter";
	case EGL_BAD_NATIVE_PIXMAP:
		return "Bad native pixmap";
	case EGL_BAD_NATIVE_WINDOW:
		return "Bad native window";
	case EGL_CONTEXT_LOST:
		return "Context lost";
	default:
		return "Unknown";
	}
}

static bool egl_get_config(EGLDisplay disp, EGLint *attribs, EGLConfig *out,
		EGLint visual_id) {
	EGLint count = 0, matched = 0, ret;

	ret = eglGetConfigs(disp, NULL, 0, &count);
	if (ret == EGL_FALSE || count == 0) {
		wlr_log(L_ERROR, "eglGetConfigs returned no configs");
		return false;
	}

	EGLConfig configs[count];

	ret = eglChooseConfig(disp, attribs, configs, count, &matched);
	if (ret == EGL_FALSE) {
		wlr_log(L_ERROR, "eglChooseConfig failed");
		return false;
	}

	for (int i = 0; i < matched; ++i) {
		EGLint visual;
		if (!eglGetConfigAttrib(disp, configs[i],
				EGL_NATIVE_VISUAL_ID, &visual)) {
			continue;
		}

		if (!visual_id || visual == visual_id) {
			*out = configs[i];
			return true;
		}
	}

	wlr_log(L_ERROR, "no valid egl config found");
	return false;
}

static bool check_egl_ext(const char *egl_exts, const char *ext) {
	size_t extlen = strlen(ext);
	const char *end = egl_exts + strlen(egl_exts);

	while (egl_exts < end) {
		if (*egl_exts == ' ') {
			egl_exts++;
			continue;
		}
		size_t n = strcspn(egl_exts, " ");
		if (n == extlen && strncmp(ext, egl_exts, n) == 0) {
			return true;
		}
		egl_exts += n;
	}
	return false;
}

bool wlr_egl_init(struct wlr_egl *egl, EGLenum platform, void *remote_display,
		EGLint *config_attribs, EGLint visual_id) {
	if (!load_glapi()) {
		return false;
	}

	if (eglBindAPI(EGL_OPENGL_ES_API) == EGL_FALSE) {
		wlr_log(L_ERROR, "Failed to bind to the OpenGL ES API: %s", egl_error());
		goto error;
	}

	if (platform == EGL_PLATFORM_SURFACELESS_MESA) {
		assert(remote_display == NULL);
		egl->display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	} else {
		egl->display = eglGetPlatformDisplayEXT(platform, remote_display, NULL);
	}
	if (egl->display == EGL_NO_DISPLAY) {
		wlr_log(L_ERROR, "Failed to create EGL display: %s", egl_error());
		goto error;
	}

	EGLint major, minor;
	if (eglInitialize(egl->display, &major, &minor) == EGL_FALSE) {
		wlr_log(L_ERROR, "Failed to initialize EGL: %s", egl_error());
		goto error;
	}

	if (!egl_get_config(egl->display, config_attribs, &egl->config, visual_id)) {
		wlr_log(L_ERROR, "Failed to get EGL config");
		goto error;
	}

	static const EGLint attribs[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};

	egl->context = eglCreateContext(egl->display, egl->config,
		EGL_NO_CONTEXT, attribs);

	if (egl->context == EGL_NO_CONTEXT) {
		wlr_log(L_ERROR, "Failed to create EGL context: %s", egl_error());
		goto error;
	}

	eglMakeCurrent(egl->display, EGL_NO_SURFACE, EGL_NO_SURFACE, egl->context);
	egl->egl_exts_str = eglQueryString(egl->display, EGL_EXTENSIONS);
	egl->gl_exts_str = (const char*) glGetString(GL_EXTENSIONS);

	wlr_log(L_INFO, "Using EGL %d.%d", (int)major, (int)minor);
	wlr_log(L_INFO, "Supported EGL extensions: %s", egl->egl_exts_str);
	wlr_log(L_INFO, "EGL vendor: %s", eglQueryString(egl->display, EGL_VENDOR));
	wlr_log(L_INFO, "Using %s", glGetString(GL_VERSION));
	wlr_log(L_INFO, "GL vendor: %s", glGetString(GL_VENDOR));
	wlr_log(L_INFO, "Supported OpenGL ES extensions: %s", egl->gl_exts_str);

	if (!check_egl_ext(egl->egl_exts_str, "EGL_WL_bind_wayland_display") ||
			!check_egl_ext(egl->egl_exts_str, "EGL_KHR_image_base")) {
		wlr_log(L_ERROR, "Required egl extensions not supported");
		goto error;
	}

	egl->egl_exts.buffer_age =
		check_egl_ext(egl->egl_exts_str, "EGL_EXT_buffer_age");
	egl->egl_exts.swap_buffers_with_damage =
		check_egl_ext(egl->egl_exts_str, "EGL_EXT_swap_buffers_with_damage") ||
		check_egl_ext(egl->egl_exts_str, "EGL_KHR_swap_buffers_with_damage");

	egl->egl_exts.dmabuf_import =
		check_egl_ext(egl->egl_exts_str, "EGL_EXT_image_dma_buf_import");
	egl->egl_exts.dmabuf_import_modifiers =
		check_egl_ext(egl->egl_exts_str, "EGL_EXT_image_dma_buf_import_modifiers")
		&& eglQueryDmaBufFormatsEXT && eglQueryDmaBufModifiersEXT;

	return true;

error:
	eglMakeCurrent(EGL_NO_DISPLAY, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
	eglTerminate(egl->display);
	eglReleaseThread();
	return false;
}

void wlr_egl_finish(struct wlr_egl *egl) {
	eglMakeCurrent(EGL_NO_DISPLAY, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
	if (egl->wl_display && eglUnbindWaylandDisplayWL) {
		eglUnbindWaylandDisplayWL(egl->display, egl->wl_display);
	}

	eglDestroyContext(egl->display, egl->context);
	eglTerminate(egl->display);
	eglReleaseThread();
}

bool wlr_egl_bind_display(struct wlr_egl *egl, struct wl_display *local_display) {
	if (!eglBindWaylandDisplayWL) {
		return false;
	}

	if (eglBindWaylandDisplayWL(egl->display, local_display)) {
		egl->wl_display = local_display;
		return true;
	}

	return false;
}

bool wlr_egl_query_buffer(struct wlr_egl *egl, struct wl_resource *buf,
		int attrib, int *value) {
	if (!eglQueryWaylandBufferWL) {
		return false;
	}
	return eglQueryWaylandBufferWL(egl->display, buf, attrib, value);
}

EGLImage wlr_egl_create_image(struct wlr_egl *egl, EGLenum target,
		EGLClientBuffer buffer, const EGLint *attribs) {
	if (!eglCreateImageKHR) {
		return NULL;
	}

	return eglCreateImageKHR(egl->display, egl->context, target,
		buffer, attribs);
}

bool wlr_egl_destroy_image(struct wlr_egl *egl, EGLImage image) {
	if (!eglDestroyImageKHR) {
		return false;
	}

	eglDestroyImageKHR(egl->display, image);
	return true;
}

EGLSurface wlr_egl_create_surface(struct wlr_egl *egl, void *window) {
	EGLSurface surf = eglCreatePlatformWindowSurfaceEXT(egl->display, egl->config,
		window, NULL);
	if (surf == EGL_NO_SURFACE) {
		wlr_log(L_ERROR, "Failed to create EGL surface: %s", egl_error());
		return EGL_NO_SURFACE;
	}
	return surf;
}

int wlr_egl_get_buffer_age(struct wlr_egl *egl, EGLSurface surface) {
	if (!egl->egl_exts.buffer_age) {
		return -1;
	}

	EGLint buffer_age;
	EGLBoolean ok = eglQuerySurface(egl->display, surface,
		EGL_BUFFER_AGE_EXT, &buffer_age);
	if (!ok) {
		wlr_log(L_ERROR, "Failed to get EGL surface buffer age: %s", egl_error());
		return -1;
	}

	return buffer_age;
}

bool wlr_egl_make_current(struct wlr_egl *egl, EGLSurface surface,
		int *buffer_age) {
	if (!eglMakeCurrent(egl->display, surface, surface, egl->context)) {
		wlr_log(L_ERROR, "eglMakeCurrent failed: %s", egl_error());
		return false;
	}

	if (buffer_age != NULL) {
		*buffer_age = wlr_egl_get_buffer_age(egl, surface);
	}
	return true;
}

bool wlr_egl_swap_buffers(struct wlr_egl *egl, EGLSurface surface,
		pixman_region32_t *damage) {
	EGLBoolean ret;
	if (damage != NULL && egl->egl_exts.swap_buffers_with_damage) {
		int nrects;
		pixman_box32_t *rects =
			pixman_region32_rectangles(damage, &nrects);
		EGLint egl_damage[4 * nrects];
		for (int i = 0; i < nrects; ++i) {
			egl_damage[4*i] = rects[i].x1;
			egl_damage[4*i + 1] = rects[i].y1;
			egl_damage[4*i + 2] = rects[i].x2 - rects[i].x1;
			egl_damage[4*i + 3] = rects[i].y2 - rects[i].y1;
		}

		assert(eglSwapBuffersWithDamageEXT || eglSwapBuffersWithDamageKHR);
		if (eglSwapBuffersWithDamageEXT) {
			ret = eglSwapBuffersWithDamageEXT(egl->display, surface, egl_damage,
				nrects);
		} else {
			ret = eglSwapBuffersWithDamageKHR(egl->display, surface, egl_damage,
				nrects);
		}
	} else {
		ret = eglSwapBuffers(egl->display, surface);
	}

	if (!ret) {
		wlr_log(L_ERROR, "eglSwapBuffers failed: %s", egl_error());
		return false;
	}
	return true;
}

EGLImage wlr_egl_create_image_from_dmabuf(struct wlr_egl *egl,
		struct wlr_dmabuf_buffer_attribs *attributes) {
	int atti = 0;
	EGLint attribs[20];
	attribs[atti++] = EGL_WIDTH;
	attribs[atti++] = attributes->width;
	attribs[atti++] = EGL_HEIGHT;
	attribs[atti++] = attributes->height;
	attribs[atti++] = EGL_LINUX_DRM_FOURCC_EXT;
	attribs[atti++] = attributes->format;

	bool has_modifier = false;
	if (attributes->modifier[0] != DRM_FORMAT_MOD_INVALID) {
		if (!egl->egl_exts.dmabuf_import_modifiers) {
			return NULL;
		}
		has_modifier = true;
	}

	/* TODO: YUV planes have up four planes but we only support a
	   single EGLImage for now */
	if (attributes->n_planes > 1) {
		return NULL;
	}

	attribs[atti++] = EGL_DMA_BUF_PLANE0_FD_EXT;
	attribs[atti++] = attributes->fd[0];
	attribs[atti++] = EGL_DMA_BUF_PLANE0_OFFSET_EXT;
	attribs[atti++] = attributes->offset[0];
	attribs[atti++] = EGL_DMA_BUF_PLANE0_PITCH_EXT;
	attribs[atti++] = attributes->stride[0];
	if (has_modifier) {
		attribs[atti++] = EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT;
		attribs[atti++] = attributes->modifier[0] & 0xFFFFFFFF;
		attribs[atti++] = EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT;
		attribs[atti++] = attributes->modifier[0] >> 32;
	}
	attribs[atti++] = EGL_NONE;
	return eglCreateImageKHR(egl->display, EGL_NO_CONTEXT,
		EGL_LINUX_DMA_BUF_EXT, NULL, attribs);
}

#ifndef DRM_FORMAT_BIG_ENDIAN
# define DRM_FORMAT_BIG_ENDIAN 0x80000000
#endif
bool wlr_egl_check_import_dmabuf(struct wlr_egl *egl,
		struct wlr_dmabuf_buffer *dmabuf) {
	switch (dmabuf->attributes.format & ~DRM_FORMAT_BIG_ENDIAN) {
		/* YUV based formats not yet supported */
	case WL_SHM_FORMAT_YUYV:
	case WL_SHM_FORMAT_YVYU:
	case WL_SHM_FORMAT_UYVY:
	case WL_SHM_FORMAT_VYUY:
	case WL_SHM_FORMAT_AYUV:
		return false;
	default:
		break;
	}

	EGLImage egl_image = wlr_egl_create_image_from_dmabuf(egl,
		&dmabuf->attributes);
	if (egl_image) {
		/* We can import the image, good. No need to keep it
		   since wlr_texture_upload_dmabuf will import it again */
		wlr_egl_destroy_image(egl, egl_image);
		return true;
	}
	/* TODO: import yuv dmabufs */
	return false;
}

int wlr_egl_get_dmabuf_formats(struct wlr_egl *egl,
		int **formats) {
	if (!egl->egl_exts.dmabuf_import ||
		!egl->egl_exts.dmabuf_import_modifiers) {
		wlr_log(L_ERROR, "dmabuf extension not present");
		return -1;
	}

	EGLint num;
	if (!eglQueryDmaBufFormatsEXT(egl->display, 0, NULL, &num)) {
		wlr_log(L_ERROR, "failed to query number of dmabuf formats");
		return -1;
	}

	*formats = calloc(num, sizeof(int));
	if (*formats == NULL) {
		wlr_log(L_ERROR, "Allocation failed: %s", strerror(errno));
		return -1;
	}

	if (!eglQueryDmaBufFormatsEXT(egl->display, num, *formats, &num)) {
		wlr_log(L_ERROR, "failed to query dmabuf format");
		free(*formats);
		return -1;
	}
	return num;
}

int wlr_egl_get_dmabuf_modifiers(struct wlr_egl *egl,
		int format, uint64_t **modifiers) {
	if (!egl->egl_exts.dmabuf_import ||
		!egl->egl_exts.dmabuf_import_modifiers) {
		wlr_log(L_ERROR, "dmabuf extension not present");
		return -1;
	}

	EGLint num;
	if (!eglQueryDmaBufModifiersEXT(egl->display, format, 0,
			NULL, NULL, &num)) {
		wlr_log(L_ERROR, "failed to query dmabuf number of modifiers");
		return -1;
	}

	*modifiers = calloc(num, sizeof(uint64_t));
	if (*modifiers == NULL) {
		wlr_log(L_ERROR, "Allocation failed: %s", strerror(errno));
		return -1;
	}

	if (!eglQueryDmaBufModifiersEXT(egl->display, format, num,
		*modifiers, NULL, &num)) {
		wlr_log(L_ERROR, "failed to query dmabuf modifiers");
		free(*modifiers);
		return -1;
	}
	return num;
}
