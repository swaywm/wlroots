#ifndef RENDER_EGL_H
#define RENDER_EGL_H

#include <wlr/render/egl.h>

struct wlr_egl {
	EGLDisplay display;
	EGLContext context;
	EGLDeviceEXT device; // may be EGL_NO_DEVICE_EXT
	struct gbm_device *gbm_device;

	struct {
		// Display extensions
		bool KHR_image_base;
		bool EXT_image_dma_buf_import;
		bool EXT_image_dma_buf_import_modifiers;

		// Device extensions
		bool EXT_device_drm;
		bool EXT_device_drm_render_node;

		// Client extensions
		bool EXT_device_query;
		bool KHR_platform_gbm;
		bool EXT_platform_device;
	} exts;

	struct {
		PFNEGLGETPLATFORMDISPLAYEXTPROC eglGetPlatformDisplayEXT;
		PFNEGLCREATEIMAGEKHRPROC eglCreateImageKHR;
		PFNEGLDESTROYIMAGEKHRPROC eglDestroyImageKHR;
		PFNEGLQUERYWAYLANDBUFFERWL eglQueryWaylandBufferWL;
		PFNEGLQUERYDMABUFFORMATSEXTPROC eglQueryDmaBufFormatsEXT;
		PFNEGLQUERYDMABUFMODIFIERSEXTPROC eglQueryDmaBufModifiersEXT;
		PFNEGLDEBUGMESSAGECONTROLKHRPROC eglDebugMessageControlKHR;
		PFNEGLQUERYDISPLAYATTRIBEXTPROC eglQueryDisplayAttribEXT;
		PFNEGLQUERYDEVICESTRINGEXTPROC eglQueryDeviceStringEXT;
		PFNEGLQUERYDEVICESEXTPROC eglQueryDevicesEXT;
	} procs;

	struct wlr_drm_format_set dmabuf_texture_formats;
	struct wlr_drm_format_set dmabuf_render_formats;
};

/**
 * Initializes an EGL context for the given DRM FD.
 *
 * Will attempt to load all possibly required API functions.
 */
struct wlr_egl *wlr_egl_create_with_drm_fd(int drm_fd);

/**
 * Frees all related EGL resources, makes the context not-current and
 * unbinds a bound wayland display.
 */
void wlr_egl_destroy(struct wlr_egl *egl);

/**
 * Creates an EGL image from the given dmabuf attributes. Check usability
 * of the dmabuf with wlr_egl_check_import_dmabuf once first.
 */
EGLImageKHR wlr_egl_create_image_from_dmabuf(struct wlr_egl *egl,
	struct wlr_dmabuf_attributes *attributes, bool *external_only);

/**
 * Get DMA-BUF formats suitable for sampling usage.
 */
const struct wlr_drm_format_set *wlr_egl_get_dmabuf_texture_formats(
	struct wlr_egl *egl);
/**
 * Get DMA-BUF formats suitable for rendering usage.
 */
const struct wlr_drm_format_set *wlr_egl_get_dmabuf_render_formats(
	struct wlr_egl *egl);

/**
 * Destroys an EGL image created with the given wlr_egl.
 */
bool wlr_egl_destroy_image(struct wlr_egl *egl, EGLImageKHR image);

int wlr_egl_dup_drm_fd(struct wlr_egl *egl);

/**
 * Makes the provided EGL context current
 *
 * Callers are expected to clear the current context when they are done by
 * calling wlr_egl_unset_current.
 */
bool wlr_egl_make_current(struct wlr_egl *egl);

/**
 * Clears the current EGLContext
 */
bool wlr_egl_unset_current(struct wlr_egl *egl);

bool wlr_egl_is_current(struct wlr_egl *egl);

#endif
