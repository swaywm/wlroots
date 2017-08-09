#ifndef WLR_BACKEND_EGL_H
#define WLR_BACKEND_EGL_H

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <stdbool.h>

struct wlr_egl {
	EGLDisplay display;
	EGLConfig config;
	EGLContext context;

	const char *egl_exts;
	const char *gl_exts;

	struct wl_display *wl_display;
};

/**
 *  Initializes an egl context for the given platform and remote display.
 * Will attempt to load all possibly required api functions.
 */
bool wlr_egl_init(struct wlr_egl *egl, EGLenum platform, void *display);

/**
 * Frees all related egl resources, makes the context not-current and
 * unbinds a bound wayland display.
 */
void wlr_egl_free(struct wlr_egl *egl);

/**
 * Binds the given display to the egl instance.
 * This will allow clients to create egl surfaces from wayland ones and render to it.
 */
bool wlr_egl_bind_display(struct wlr_egl *egl, struct wl_display *local_display);

/**
 * Queries information about the given (potential egl/drm) buffer, returns
 * the information in value.
 * Refer to eglQueryWaylandBufferWL for more information about attrib and value.
 * Makes only sense when a wl_display was bound to it since otherwise there
 * cannot be any egl/drm buffers.
 * Will only work after a wlr_egl objct was initialized and if the needed egl extension
 * is present.
 */
bool wlr_egl_query_buffer(struct wl_resource *buf,
	EGLint attrib, EGLint *value);

/**
 * Returns a surface for the given native window
 * The window must match the remote display the wlr_egl was created with.
 */
EGLSurface wlr_egl_create_surface(struct wlr_egl *egl, void *window);

/**
 * Creates an egl image from the given client buffer and attributes.
 * Will only work after a wlr_egl objct was initialized and if the needed egl extension
 * is present.
 */
EGLImageKHR wlr_egl_create_image(EGLenum target, EGLClientBuffer buffer,
	const EGLint *attribs);

/**
 * Destroys an egl image created with the given wlr_egl.
 */
bool wlr_egl_destroy_image(EGLImageKHR image);

/**
 * Returns a string for the last error ocurred with egl.
 */
const char *egl_error(void);

#endif
