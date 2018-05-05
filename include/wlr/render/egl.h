#ifndef WLR_RENDER_EGL_H
#define WLR_RENDER_EGL_H

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <pixman.h>
#include <stdbool.h>
#include <wayland-server.h>
#include <wlr/types/wlr_linux_dmabuf.h>

struct wlr_egl {
	EGLDisplay display;
	EGLConfig config;
	EGLContext context;

	const char *exts_str;

	struct {
		bool buffer_age;
		bool swap_buffers_with_damage;
		bool dmabuf_import;
		bool dmabuf_import_modifiers;
		bool bind_wayland_display;
	} egl_exts;

	struct wl_display *wl_display;

	struct {
		struct wl_signal destroy;
	} events;
};

// TODO: Allocate and return a wlr_egl
/**
 * Initializes an EGL context for the given platform and remote display.
 * Will attempt to load all possibly required api functions.
 */
bool wlr_egl_init(struct wlr_egl *egl, EGLenum platform, void *remote_display,
	EGLint *config_attribs, EGLint visual_id);

/**
 * Frees all related EGL resources, makes the context not-current and
 * unbinds a bound wayland display.
 */
void wlr_egl_finish(struct wlr_egl *egl);

/**
 * Binds the given wl_display to the EGL instance. This will allow clients to
 * create EGL surfaces from Wayland ones and render to it via the deprecated
 * wl_drm interface.
 */
bool wlr_egl_bind_wl_display(struct wlr_egl *egl, struct wl_display *display);

/**
 * Returns a surface for the given native window.
 * The window must match the remote display the wlr_egl was created with.
 */
EGLSurface wlr_egl_create_surface(struct wlr_egl *egl, void *window);

/**
 * Creates an EGL image from the given wl_drm buffer resource.
 */
EGLImageKHR wlr_egl_create_image_from_wl_drm(struct wlr_egl *egl,
	struct wl_resource *data, EGLint *fmt, int *width, int *height,
	bool *inverted_y);

/**
 * Creates an EGL image from the given dmabuf attributes. Check usability
 * of the dmabuf with wlr_egl_check_import_dmabuf once first.
 */
EGLImageKHR wlr_egl_create_image_from_dmabuf(struct wlr_egl *egl,
	struct wlr_dmabuf_buffer_attribs *attributes);

/**
 * Try to import the given dmabuf. On success return true false otherwise.
 * If this succeeds the dmabuf can be used for rendering on a texture
 */
bool wlr_egl_check_import_dmabuf(struct wlr_egl *egl,
	struct wlr_dmabuf_buffer *dmabuf);

/**
 * Get the available dmabuf formats
 */
int wlr_egl_get_dmabuf_formats(struct wlr_egl *egl, int **formats);

/**
 * Get the available dmabuf modifiers for a given format
 */
int wlr_egl_get_dmabuf_modifiers(struct wlr_egl *egl, int format,
	uint64_t **modifiers);

/**
 * Destroys an EGL image created with the given wlr_egl.
 */
bool wlr_egl_destroy_image(struct wlr_egl *egl, EGLImageKHR image);

/**
 * Makes the EGL context current. If `buffer_age` is not NULL, sets it to the
 * current buffer age, or -1 if unknown.
 */
bool wlr_egl_make_current(struct wlr_egl *egl, EGLSurface surface,
	int *buffer_age);

/**
 * Checks if the EGL context is the current one.
 */
bool wlr_egl_is_current(struct wlr_egl *egl);

/**
 * Swaps buffers. The buffer damage is optional.
 */
bool wlr_egl_swap_buffers(struct wlr_egl *egl, EGLSurface surface,
	pixman_region32_t *damage);

/**
 * Destroys the EGL surface.
 */
bool wlr_egl_destroy_surface(struct wlr_egl *egl, EGLSurface surface);

#endif
