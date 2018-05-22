#include <assert.h>
#include <stdio.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <stdlib.h>
#include <wlr/render/egl.h>
#include <wlr/util/log.h>
#include "glapi.h"

// Extension documentation
// https://www.khronos.org/registry/EGL/extensions/KHR/EGL_KHR_image_base.txt.
// https://cgit.freedesktop.org/mesa/mesa/tree/docs/specs/WL_bind_wayland_display.spec

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

static log_importance_t egl_log_importance_to_wlr(EGLint type) {
	switch (type) {
	case EGL_DEBUG_MSG_CRITICAL_KHR: return L_ERROR;
	case EGL_DEBUG_MSG_ERROR_KHR:    return L_ERROR;
	case EGL_DEBUG_MSG_WARN_KHR:     return L_ERROR;
	case EGL_DEBUG_MSG_INFO_KHR:     return L_INFO;
	default:                         return L_INFO;
	}
}

static void egl_log(EGLenum error, const char *command, EGLint msg_type,
		EGLLabelKHR thread, EGLLabelKHR obj, const char *msg) {
	_wlr_log(egl_log_importance_to_wlr(msg_type), "[EGL] %s: %s", command, msg);
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

static void print_dmabuf_formats(struct wlr_egl *egl) {
	/* Avoid log msg if extension is not present */
	if (!egl->egl_exts.dmabuf_import_modifiers) {
		return;
	}

	int *formats;
	int num = wlr_egl_get_dmabuf_formats(egl, &formats);
	if (num < 0) {
		return;
	}

	char str_formats[num * 5 + 1];
	for (int i = 0; i < num; i++) {
		snprintf(&str_formats[i*5], (num - i) * 5 + 1, "%.4s ",
			(char*)&formats[i]);
	}
	wlr_log(L_DEBUG, "Supported dmabuf buffer formats: %s", str_formats);
	free(formats);
}

bool wlr_egl_init(struct wlr_egl *egl, EGLenum platform, void *remote_display,
		EGLint *config_attribs, EGLint visual_id) {
	if (!load_glapi()) {
		return false;
	}

	if (eglDebugMessageControlKHR) {
		static const EGLAttrib debug_attribs[] = {
			EGL_DEBUG_MSG_CRITICAL_KHR, EGL_TRUE,
			EGL_DEBUG_MSG_ERROR_KHR, EGL_TRUE,
			EGL_DEBUG_MSG_WARN_KHR, EGL_TRUE,
			EGL_DEBUG_MSG_INFO_KHR, EGL_TRUE,
			EGL_NONE,
		};
		eglDebugMessageControlKHR(egl_log, debug_attribs);
	}

	if (eglBindAPI(EGL_OPENGL_ES_API) == EGL_FALSE) {
		wlr_log(L_ERROR, "Failed to bind to the OpenGL ES API");
		goto error;
	}

	if (platform == EGL_PLATFORM_SURFACELESS_MESA) {
		assert(remote_display == NULL);
		egl->display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	} else {
		egl->display = eglGetPlatformDisplayEXT(platform, remote_display, NULL);
	}
	if (egl->display == EGL_NO_DISPLAY) {
		wlr_log(L_ERROR, "Failed to create EGL display");
		goto error;
	}

	EGLint major, minor;
	if (eglInitialize(egl->display, &major, &minor) == EGL_FALSE) {
		wlr_log(L_ERROR, "Failed to initialize EGL");
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
		wlr_log(L_ERROR, "Failed to create EGL context");
		goto error;
	}

	eglMakeCurrent(egl->display, EGL_NO_SURFACE, EGL_NO_SURFACE, egl->context);
	egl->exts_str = eglQueryString(egl->display, EGL_EXTENSIONS);

	wlr_log(L_INFO, "Using EGL %d.%d", (int)major, (int)minor);
	wlr_log(L_INFO, "Supported EGL extensions: %s", egl->exts_str);
	wlr_log(L_INFO, "EGL vendor: %s", eglQueryString(egl->display, EGL_VENDOR));

	if (!check_egl_ext(egl->exts_str, "EGL_KHR_image_base")) {
		wlr_log(L_ERROR, "Required EGL_KHR_image_base extension not supported");
		goto error;
	}

	egl->egl_exts.buffer_age =
		check_egl_ext(egl->exts_str, "EGL_EXT_buffer_age");
	egl->egl_exts.swap_buffers_with_damage =
		check_egl_ext(egl->exts_str, "EGL_EXT_swap_buffers_with_damage") ||
		check_egl_ext(egl->exts_str, "EGL_KHR_swap_buffers_with_damage");

	egl->egl_exts.dmabuf_import =
		check_egl_ext(egl->exts_str, "EGL_EXT_image_dma_buf_import");
	egl->egl_exts.dmabuf_import_modifiers =
		check_egl_ext(egl->exts_str, "EGL_EXT_image_dma_buf_import_modifiers")
		&& eglQueryDmaBufFormatsEXT && eglQueryDmaBufModifiersEXT;

	egl->egl_exts.dmabuf_export =
		check_egl_ext(egl->exts_str, "EGL_MESA_image_dma_buf_export");

	egl->egl_exts.bind_wayland_display =
		check_egl_ext(egl->exts_str, "EGL_WL_bind_wayland_display");

	print_dmabuf_formats(egl);

	return true;

error:
	eglMakeCurrent(EGL_NO_DISPLAY, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
	if (egl->display) {
		eglTerminate(egl->display);
	}
	eglReleaseThread();
	return false;
}

void wlr_egl_finish(struct wlr_egl *egl) {
	if (egl == NULL) {
		return;
	}

	eglMakeCurrent(egl->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
	if (egl->wl_display && egl->egl_exts.bind_wayland_display) {
		eglUnbindWaylandDisplayWL(egl->display, egl->wl_display);
	}

	eglDestroyContext(egl->display, egl->context);
	eglTerminate(egl->display);
	eglReleaseThread();
}

bool wlr_egl_bind_display(struct wlr_egl *egl, struct wl_display *local_display) {
	if (!egl->egl_exts.bind_wayland_display || !eglBindWaylandDisplayWL) {
		return false;
	}

	if (eglBindWaylandDisplayWL(egl->display, local_display)) {
		egl->wl_display = local_display;
		return true;
	}

	return false;
}

bool wlr_egl_destroy_image(struct wlr_egl *egl, EGLImage image) {
	if (!eglDestroyImageKHR) {
		return false;
	}
	if (!image) {
		return true;
	}
	return eglDestroyImageKHR(egl->display, image);
}

EGLSurface wlr_egl_create_surface(struct wlr_egl *egl, void *window) {
	assert(eglCreatePlatformWindowSurfaceEXT);
	EGLSurface surf = eglCreatePlatformWindowSurfaceEXT(egl->display,
		egl->config, window, NULL);
	if (surf == EGL_NO_SURFACE) {
		wlr_log(L_ERROR, "Failed to create EGL surface");
		return EGL_NO_SURFACE;
	}
	return surf;
}

static int egl_get_buffer_age(struct wlr_egl *egl, EGLSurface surface) {
	if (!egl->egl_exts.buffer_age) {
		return -1;
	}

	EGLint buffer_age;
	EGLBoolean ok = eglQuerySurface(egl->display, surface,
		EGL_BUFFER_AGE_EXT, &buffer_age);
	if (!ok) {
		wlr_log(L_ERROR, "Failed to get EGL surface buffer age");
		return -1;
	}

	return buffer_age;
}

bool wlr_egl_make_current(struct wlr_egl *egl, EGLSurface surface,
		int *buffer_age) {
	if (!eglMakeCurrent(egl->display, surface, surface, egl->context)) {
		wlr_log(L_ERROR, "eglMakeCurrent failed");
		return false;
	}

	if (buffer_age != NULL) {
		*buffer_age = egl_get_buffer_age(egl, surface);
	}
	return true;
}

bool wlr_egl_is_current(struct wlr_egl *egl) {
	return eglGetCurrentContext() == egl->context;
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
		wlr_log(L_ERROR, "eglSwapBuffers failed");
		return false;
	}
	return true;
}

EGLImageKHR wlr_egl_create_image_from_wl_drm(struct wlr_egl *egl,
		struct wl_resource *data, EGLint *fmt, int *width, int *height,
		bool *inverted_y) {
	if (!eglQueryWaylandBufferWL || !eglCreateImageKHR) {
		return NULL;
	}

	if (!eglQueryWaylandBufferWL(egl->display, data, EGL_TEXTURE_FORMAT, fmt)) {
		return NULL;
	}

	eglQueryWaylandBufferWL(egl->display, data, EGL_WIDTH, width);
	eglQueryWaylandBufferWL(egl->display, data, EGL_HEIGHT, height);

	EGLint _inverted_y;
	if (eglQueryWaylandBufferWL(egl->display, data, EGL_WAYLAND_Y_INVERTED_WL,
			&_inverted_y)) {
		*inverted_y = !!_inverted_y;
	} else {
		*inverted_y = false;
	}

	const EGLint attribs[] = {
		EGL_WAYLAND_PLANE_WL, 0,
		EGL_NONE,
	};
	return eglCreateImageKHR(egl->display, egl->context, EGL_WAYLAND_BUFFER_WL,
		data, attribs);
}

EGLImageKHR wlr_egl_create_image_from_dmabuf(struct wlr_egl *egl,
		struct wlr_dmabuf_buffer_attribs *attributes) {
	bool has_modifier = false;
	if (attributes->modifier[0] != DRM_FORMAT_MOD_INVALID) {
		if (!egl->egl_exts.dmabuf_import_modifiers) {
			return NULL;
		}
		has_modifier = true;
	}

	unsigned int atti = 0;
	EGLint attribs[50];
	attribs[atti++] = EGL_WIDTH;
	attribs[atti++] = attributes->width;
	attribs[atti++] = EGL_HEIGHT;
	attribs[atti++] = attributes->height;
	attribs[atti++] = EGL_LINUX_DRM_FOURCC_EXT;
	attribs[atti++] = attributes->format;

	struct {
		EGLint fd;
		EGLint offset;
		EGLint pitch;
		EGLint mod_lo;
		EGLint mod_hi;
	} attr_names[WLR_LINUX_DMABUF_MAX_PLANES] = {
		{
			EGL_DMA_BUF_PLANE0_FD_EXT,
			EGL_DMA_BUF_PLANE0_OFFSET_EXT,
			EGL_DMA_BUF_PLANE0_PITCH_EXT,
			EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT,
			EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT
		}, {
			EGL_DMA_BUF_PLANE1_FD_EXT,
			EGL_DMA_BUF_PLANE1_OFFSET_EXT,
			EGL_DMA_BUF_PLANE1_PITCH_EXT,
			EGL_DMA_BUF_PLANE1_MODIFIER_LO_EXT,
			EGL_DMA_BUF_PLANE1_MODIFIER_HI_EXT
		}, {
			EGL_DMA_BUF_PLANE2_FD_EXT,
			EGL_DMA_BUF_PLANE2_OFFSET_EXT,
			EGL_DMA_BUF_PLANE2_PITCH_EXT,
			EGL_DMA_BUF_PLANE2_MODIFIER_LO_EXT,
			EGL_DMA_BUF_PLANE2_MODIFIER_HI_EXT
		}, {
			EGL_DMA_BUF_PLANE3_FD_EXT,
			EGL_DMA_BUF_PLANE3_OFFSET_EXT,
			EGL_DMA_BUF_PLANE3_PITCH_EXT,
			EGL_DMA_BUF_PLANE3_MODIFIER_LO_EXT,
			EGL_DMA_BUF_PLANE3_MODIFIER_HI_EXT
		}
	};

	for (int i=0; i < attributes->n_planes; i++) {
		attribs[atti++] = attr_names[i].fd;
		attribs[atti++] = attributes->fd[i];
		attribs[atti++] = attr_names[i].offset;
		attribs[atti++] = attributes->offset[i];
		attribs[atti++] = attr_names[i].pitch;
		attribs[atti++] = attributes->stride[i];
		if (has_modifier) {
			attribs[atti++] = attr_names[i].mod_lo;
			attribs[atti++] = attributes->modifier[i] & 0xFFFFFFFF;
			attribs[atti++] = attr_names[i].mod_hi;
			attribs[atti++] = attributes->modifier[i] >> 32;
		}
	}
	attribs[atti++] = EGL_NONE;
	assert(atti < sizeof(attribs)/sizeof(attribs[0]));

	return eglCreateImageKHR(egl->display, EGL_NO_CONTEXT,
		EGL_LINUX_DMA_BUF_EXT, NULL, attribs);
}

#ifndef DRM_FORMAT_BIG_ENDIAN
#define DRM_FORMAT_BIG_ENDIAN 0x80000000
#endif
bool wlr_egl_check_import_dmabuf(struct wlr_egl *egl,
		struct wlr_dmabuf_buffer *dmabuf) {
	switch (dmabuf->attributes.format & ~DRM_FORMAT_BIG_ENDIAN) {
		/* TODO: YUV based formats not yet supported, require multiple
		 * wlr_create_image_from_dmabuf */
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
		wlr_log(L_DEBUG, "dmabuf extension not present");
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
		wlr_log(L_DEBUG, "dmabuf extension not present");
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

bool wlr_egl_export_image_to_dmabuf(struct wlr_egl *egl, EGLImageKHR image,
		int32_t width, int32_t height, uint32_t flags,
		struct wlr_dmabuf_buffer_attribs *attribs) {
	memset(attribs, 0, sizeof(struct wlr_dmabuf_buffer_attribs));

	if (!egl->egl_exts.dmabuf_export || !eglExportDMABUFImageQueryMESA ||
			!eglExportDMABUFImageMESA) {
		return false;
	}

	// Only one set of modifiers is returned for all planes
	EGLuint64KHR modifiers;
	if (!eglExportDMABUFImageQueryMESA(egl->display, image,
			(int *)&attribs->format, &attribs->n_planes, &modifiers)) {
		return false;
	}
	if (attribs->n_planes > WLR_LINUX_DMABUF_MAX_PLANES) {
		wlr_log(L_ERROR, "EGL returned %d planes, but only %d are supported",
			attribs->n_planes, WLR_LINUX_DMABUF_MAX_PLANES);
		return false;
	}
	for (int i = 0; i < attribs->n_planes; ++i) {
		attribs->modifier[i] = modifiers;
	}

	if (!eglExportDMABUFImageMESA(egl->display, image, attribs->fd,
			(EGLint *)attribs->stride, (EGLint *)attribs->offset)) {
		return false;
	}

	attribs->width = width;
	attribs->height = height;
	attribs->flags = flags;
	return true;
}

bool wlr_egl_destroy_surface(struct wlr_egl *egl, EGLSurface surface) {
	if (!surface) {
		return true;
	}
	return eglDestroySurface(egl->display, surface);
}
