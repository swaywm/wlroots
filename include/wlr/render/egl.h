#ifndef WLR_EGL_H
#define WLR_EGL_H

#include <stdbool.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <pixman.h>
#include <wayland-server.h>

struct wlr_egl {
	EGLDisplay display;
	EGLConfig config;
	EGLContext context;

	const char *egl_exts_str;
	const char *gl_exts_str;

	struct {
		bool buffer_age;
		bool swap_buffers_with_damage;
	} egl_exts;

	struct wl_display *wl_display;
};

// TODO: Allocate and return a wlr_egl
/**
 *  Initializes an egl context for the given platform and remote display.
 * Will attempt to load all possibly required api functions.
 */
bool wlr_egl_init(struct wlr_egl *egl, EGLenum platform, void *remote_display,
	EGLint *config_attribs, EGLint visual_id);

/**
 * Frees all related egl resources, makes the context not-current and
 * unbinds a bound wayland display.
 */
void wlr_egl_finish(struct wlr_egl *egl);

/**
 * Binds the given display to the egl instance.
 * This will allow clients to create egl surfaces from wayland ones and render to it.
 */
bool wlr_egl_bind_display(struct wlr_egl *egl, struct wl_display *local_display);

/**
 * Refer to the eglQueryWaylandBufferWL extension function.
 */
bool wlr_egl_query_buffer(struct wlr_egl *egl, struct wl_resource *buf,
	EGLint attrib, EGLint *value);

/**
 * Returns a surface for the given native window
 * The window must match the remote display the wlr_egl was created with.
 */
EGLSurface wlr_egl_create_surface(struct wlr_egl *egl, void *window);

/**
 * Creates an egl image from the given client buffer and attributes.
 */
EGLImageKHR wlr_egl_create_image(struct wlr_egl *egl,
		EGLenum target, EGLClientBuffer buffer, const EGLint *attribs);

/**
 * Destroys an egl image created with the given wlr_egl.
 */
bool wlr_egl_destroy_image(struct wlr_egl *egl, EGLImageKHR image);

/**
 * Returns a string for the last error ocurred with egl.
 */
const char *egl_error(void);

bool wlr_egl_make_current(struct wlr_egl *egl, EGLSurface surface,
	int *buffer_age);

bool wlr_egl_swap_buffers(struct wlr_egl *egl, EGLSurface surface,
	pixman_region32_t *damage);

#endif
