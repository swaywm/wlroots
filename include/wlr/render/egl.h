/*
 * This an unstable interface of wlroots. No guarantees are made regarding the
 * future consistency of this API.
 */
#ifndef WLR_USE_UNSTABLE
#error "Add -DWLR_USE_UNSTABLE to enable unstable wlroots features"
#endif

#ifndef WLR_RENDER_EGL_H
#define WLR_RENDER_EGL_H

#include <wlr/config.h>

#if !WLR_HAS_X11_BACKEND && !WLR_HAS_XWAYLAND
#define MESA_EGL_NO_X11_HEADERS
#endif

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <pixman.h>
#include <stdbool.h>
#include <wayland-server.h>
#include <wlr/render/dmabuf.h>

struct wlr_egl {
	EGLenum platform;
	EGLDisplay display;
	EGLConfig config;
	EGLContext context;

	const char *exts_str;

	struct {
		bool bind_wayland_display_wl;
		bool buffer_age_ext;
		bool image_base_khr;
		bool image_dma_buf_export_mesa;
		bool image_dmabuf_import_ext;
		bool image_dmabuf_import_modifiers_ext;
		bool swap_buffers_with_damage_ext;
		bool swap_buffers_with_damage_khr;
	} exts;

	struct wl_display *wl_display;
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
 * Binds the given display to the EGL instance.
 * This will allow clients to create EGL surfaces from wayland ones and render
 * to it.
 */
bool wlr_egl_bind_display(struct wlr_egl *egl, struct wl_display *local_display);

/**
 * Returns a surface for the given native window
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
	struct wlr_dmabuf_attributes *attributes);

/**
 * Get the available dmabuf formats
 */
int wlr_egl_get_dmabuf_formats(struct wlr_egl *egl, int **formats);

/**
 * Get the available dmabuf modifiers for a given format
 */
int wlr_egl_get_dmabuf_modifiers(struct wlr_egl *egl, int format,
	uint64_t **modifiers);

bool wlr_egl_export_image_to_dmabuf(struct wlr_egl *egl, EGLImageKHR image,
	int32_t width, int32_t height, uint32_t flags,
	struct wlr_dmabuf_attributes *attribs);

/**
 * Destroys an EGL image created with the given wlr_egl.
 */
bool wlr_egl_destroy_image(struct wlr_egl *egl, EGLImageKHR image);

bool wlr_egl_make_current(struct wlr_egl *egl, EGLSurface surface,
	int *buffer_age);

bool wlr_egl_is_current(struct wlr_egl *egl);

bool wlr_egl_swap_buffers(struct wlr_egl *egl, EGLSurface surface,
	pixman_region32_t *damage);

bool wlr_egl_destroy_surface(struct wlr_egl *egl, EGLSurface surface);

#endif
