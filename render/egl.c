#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <stdlib.h>
#include <wlr/util/log.h>
#include <wlr/render/egl.h>
#include "render/glapi.h"

static log_importance_t egl_to_wlr(EGLint type) {
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
	_wlr_log(egl_to_wlr(msg_type), "[EGL] %s: %s", command, msg);
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
		goto error;
	}

	egl->display = eglGetPlatformDisplayEXT(platform, remote_display, NULL);
	if (egl->display == EGL_NO_DISPLAY) {
		goto error;
	}

	EGLint major, minor;
	if (eglInitialize(egl->display, &major, &minor) == EGL_FALSE) {
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
		goto error;
	}

	eglMakeCurrent(egl->display, EGL_NO_SURFACE, EGL_NO_SURFACE, egl->context);

	wlr_log(L_INFO, "Using EGL %d.%d", (int)major, (int)minor);
	wlr_log(L_INFO, "Supported EGL extensions: %s",
		eglQueryString(egl->display, EGL_EXTENSIONS));
	wlr_log(L_INFO, "Using %s", glGetString(GL_VERSION));
	wlr_log(L_INFO, "Supported OpenGL ES extensions: %s",
		glGetString(GL_EXTENSIONS));
	return true;

error:
	eglMakeCurrent(EGL_NO_DISPLAY, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
	if (egl->display) {
		eglTerminate(egl->display);
	}
	eglReleaseThread();
	return false;
}

void wlr_egl_free(struct wlr_egl *egl) {
	if (!egl) {
		return;
	}

	eglMakeCurrent(EGL_NO_DISPLAY, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
	if (egl->wl_display) {
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

bool wlr_egl_query_wl_drm_size(struct wlr_egl *egl, struct wl_resource *buf,
		int32_t *width, int32_t *height) {
	EGLint w, h;
	if (!eglQueryWaylandBufferWL(egl->display, buf, EGL_WIDTH, &w)) {
		return false;
	}
	if (!eglQueryWaylandBufferWL(egl->display, buf, EGL_HEIGHT, &h)) {
		return false;
	}

	*width = w;
	*height = h;
	return true;
}

EGLSurface wlr_egl_create_surface(struct wlr_egl *egl, void *window) {
	return eglCreatePlatformWindowSurfaceEXT(
		egl->display, egl->config, window, NULL);
}
