#ifndef WLR_BACKEND_EGL_H
#define WLR_BACKEND_EGL_H

#include <EGL/egl.h>
#include <stdbool.h>

struct wlr_egl {
	EGLDisplay display;
	EGLConfig config;
	EGLContext context;
};

bool wlr_egl_init(struct wlr_egl *egl, EGLenum platform, void *display);
void wlr_egl_free(struct wlr_egl *egl);
EGLSurface wlr_egl_create_surface(struct wlr_egl *egl, void *window);

#endif
