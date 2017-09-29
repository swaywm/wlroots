#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <stdlib.h>
#include <wlr/util/log.h>
#include <wlr/egl.h>

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

static bool egl_exts(struct wlr_egl *egl) {
	egl->get_platform_display = (PFNEGLGETPLATFORMDISPLAYEXTPROC)
		eglGetProcAddress("eglGetPlatformDisplayEXT");

	if (!egl->get_platform_display) {
		wlr_log(L_ERROR, "Failed to load EGL extension 'eglGetPlatformDisplayEXT'");
		return false;
	}

	egl->create_platform_window_surface = (PFNEGLCREATEPLATFORMWINDOWSURFACEEXTPROC)
		eglGetProcAddress("eglCreatePlatformWindowSurfaceEXT");

	if (!egl->create_platform_window_surface) {
		wlr_log(L_ERROR,
			"Failed to load EGL extension 'eglCreatePlatformWindowSurfaceEXT'");
		return false;
	}

	return true;
}

static bool egl_get_config(EGLDisplay disp, EGLConfig *out, EGLint visual_id) {
	EGLint count = 0, matched = 0, ret;

	ret = eglGetConfigs(disp, NULL, 0, &count);
	if (ret == EGL_FALSE || count == 0) {
		wlr_log(L_ERROR, "eglGetConfigs returned no configs");
		return false;
	}

	EGLConfig configs[count];

	ret = eglChooseConfig(disp, NULL, configs, count, &matched);
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

		if (visual == visual_id) {
			*out = configs[i];
			return true;
		}
	}

	wlr_log(L_ERROR, "no valid egl config found");
	return false;
}

bool wlr_egl_init(struct wlr_egl *egl, EGLenum platform, EGLint visual_id,
		void *remote_display) {
	if (!egl_exts(egl)) {
		return false;
	}

	if (eglBindAPI(EGL_OPENGL_ES_API) == EGL_FALSE) {
		wlr_log(L_ERROR, "Failed to bind to the OpenGL ES API: %s", egl_error());
		goto error;
	}

	egl->display = egl->get_platform_display(platform, remote_display, NULL);
	if (egl->display == EGL_NO_DISPLAY) {
		wlr_log(L_ERROR, "Failed to create EGL display: %s", egl_error());
		goto error;
	}

	EGLint major, minor;
	if (eglInitialize(egl->display, &major, &minor) == EGL_FALSE) {
		wlr_log(L_ERROR, "Failed to initialize EGL: %s", egl_error());
		goto error;
	}

	if (!egl_get_config(egl->display, &egl->config, visual_id)) {
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
	egl->egl_exts = eglQueryString(egl->display, EGL_EXTENSIONS);
	if (strstr(egl->egl_exts, "EGL_WL_bind_wayland_display") == NULL ||
		strstr(egl->egl_exts, "EGL_KHR_image_base") == NULL) {
		wlr_log(L_ERROR, "Required egl extensions not supported");
		goto error;
	}

	egl->eglCreateImageKHR = (PFNEGLCREATEIMAGEKHRPROC)
		eglGetProcAddress("eglCreateImageKHR");
	egl->eglDestroyImageKHR = (PFNEGLDESTROYIMAGEKHRPROC)
		eglGetProcAddress("eglDestroyImageKHR");
	egl->eglQueryWaylandBufferWL = (PFNEGLQUERYWAYLANDBUFFERWL)
		(void*) eglGetProcAddress("eglQueryWaylandBufferWL");
	egl->eglBindWaylandDisplayWL = (PFNEGLBINDWAYLANDDISPLAYWL)
		(void*) eglGetProcAddress("eglBindWaylandDisplayWL");
	egl->eglUnbindWaylandDisplayWL = (PFNEGLUNBINDWAYLANDDISPLAYWL)
		(void*) eglGetProcAddress("eglUnbindWaylandDisplayWL");

	egl->gl_exts = (const char*) glGetString(GL_EXTENSIONS);
	wlr_log(L_INFO, "Using EGL %d.%d", (int)major, (int)minor);
	wlr_log(L_INFO, "Supported EGL extensions: %s", egl->egl_exts);
	wlr_log(L_INFO, "Using %s", glGetString(GL_VERSION));
	wlr_log(L_INFO, "Supported OpenGL ES extensions: %s", egl->gl_exts);
	return true;

error:
	eglMakeCurrent(EGL_NO_DISPLAY, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
	eglTerminate(egl->display);
	eglReleaseThread();
	return false;
}

void wlr_egl_free(struct wlr_egl *egl) {
	eglMakeCurrent(EGL_NO_DISPLAY, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
	if (egl->wl_display && egl->eglUnbindWaylandDisplayWL) {
		egl->eglUnbindWaylandDisplayWL(egl->display, egl->wl_display);
	}

	eglDestroyContext(egl->display, egl->context);
	eglTerminate(egl->display);
	eglReleaseThread();
}

bool wlr_egl_bind_display(struct wlr_egl *egl, struct wl_display *local_display) {
	if (!egl->eglBindWaylandDisplayWL) {
		return false;
	}

	if (egl->eglBindWaylandDisplayWL(egl->display, local_display)) {
		egl->wl_display = local_display;
		return true;
	}

	return false;
}

bool wlr_egl_query_buffer(struct wlr_egl *egl, struct wl_resource *buf,
		int attrib, int *value) {
	if (!egl->eglQueryWaylandBufferWL) {
		return false;
	}
	return egl->eglQueryWaylandBufferWL(egl->display, buf, attrib, value);
}

EGLImage wlr_egl_create_image(struct wlr_egl *egl, EGLenum target,
		EGLClientBuffer buffer, const EGLint *attribs) {
	if (!egl->eglCreateImageKHR) {
		return false;
	}

	return egl->eglCreateImageKHR(egl->display, egl->context, target,
		buffer, attribs);
}

bool wlr_egl_destroy_image(struct wlr_egl *egl, EGLImage image) {
	if (!egl->eglDestroyImageKHR) {
		return false;
	}

	egl->eglDestroyImageKHR(egl->display, image);
	return true;
}

EGLSurface wlr_egl_create_surface(struct wlr_egl *egl, void *window) {
	EGLSurface surf = egl->create_platform_window_surface(egl->display, egl->config,
		window, NULL);
	if (surf == EGL_NO_SURFACE) {
		wlr_log(L_ERROR, "Failed to create EGL surface: %s", egl_error());
		return EGL_NO_SURFACE;
	}
	return surf;
}
