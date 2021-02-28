/*
 * This an unstable interface of wlroots. No guarantees are made regarding the
 * future consistency of this API.
 */
#ifndef WLR_USE_UNSTABLE
#error "Add -DWLR_USE_UNSTABLE to enable unstable wlroots features"
#endif

#ifndef WLR_RENDER_EGL_H
#define WLR_RENDER_EGL_H

#ifndef MESA_EGL_NO_X11_HEADERS
#define MESA_EGL_NO_X11_HEADERS
#endif
#ifndef EGL_NO_X11
#define EGL_NO_X11
#endif
#ifndef EGL_NO_PLATFORM_SPECIFIC_TYPES
#define EGL_NO_PLATFORM_SPECIFIC_TYPES
#endif

#include <wlr/config.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <pixman.h>
#include <stdbool.h>
#include <wayland-server-core.h>
#include <wlr/render/dmabuf.h>
#include <wlr/render/drm_format_set.h>

#ifndef EGL_NV_stream_attrib
#define EGL_NV_stream_attrib 1
typedef EGLStreamKHR (EGLAPIENTRYP PFNEGLCREATESTREAMATTRIBNVPROC)(EGLDisplay dpy, const EGLAttrib *attrib_list);
typedef EGLBoolean (EGLAPIENTRYP PFNEGLSETSTREAMATTRIBNVPROC)(EGLDisplay dpy, EGLStreamKHR stream, EGLenum attribute, EGLAttrib value);
typedef EGLBoolean (EGLAPIENTRYP PFNEGLQUERYSTREAMATTRIBNVPROC)(EGLDisplay dpy, EGLStreamKHR stream, EGLenum attribute, EGLAttrib *value);
typedef EGLBoolean (EGLAPIENTRYP PFNEGLSTREAMCONSUMERACQUIREATTRIBNVPROC)(EGLDisplay dpy, EGLStreamKHR stream, const EGLAttrib *attrib_list);
typedef EGLBoolean (EGLAPIENTRYP PFNEGLSTREAMCONSUMERRELEASEATTRIBNVPROC)(EGLDisplay dpy, EGLStreamKHR stream, const EGLAttrib *attrib_list);
typedef EGLBoolean (EGLAPIENTRYP PFNEGLQUERYSTREAMATTRIBNV)(EGLDisplay, EGLStreamKHR, EGLenum, EGLAttrib *);
typedef EGLBoolean (EGLAPIENTRYP PFNEGLSTREAMCONSUMERRELEASEKHR)(EGLDisplay, EGLStreamKHR);
#endif /* EGL_NV_stream_attrib */

#ifndef EGL_EXT_stream_acquire_mode
#define EGL_EXT_stream_acquire_mode 1
#define EGL_CONSUMER_AUTO_ACQUIRE_EXT 0x332B
typedef EGLBoolean (EGLAPIENTRYP PFNEGLSTREAMCONSUMERACQUIREATTRIBEXTPROC)(EGLDisplay dpy, EGLStreamKHR stream, const EGLAttrib *attrib_list);
#endif /* EGL_EXT_stream_acquire_mode */

#ifndef EGL_NV_output_drm_flip_event
#define EGL_NV_output_drm_flip_event 1
#define EGL_DRM_FLIP_EVENT_DATA_NV 0x333E
#endif /* EGL_NV_output_drm_flip_event */

#ifndef EGL_DRM_MASTER_FD_EXT
#define EGL_DRM_MASTER_FD_EXT 0x333C
#endif /* EGL_DRM_MASTER_FD_EXT */

#ifndef EGL_WL_wayland_eglstream
#define EGL_WL_wayland_eglstream 1
#define EGL_WAYLAND_EGLSTREAM_WL 0x334B
#endif /* EGL_WL_wayland_eglstream */

#ifndef EGL_RESOURCE_BUSY_EXT
#define EGL_RESOURCE_BUSY_EXT 0x3353
#endif /* EGL_RESOURCE_BUSY_EXT */

struct wlr_egl_context {
	EGLDisplay display;
	EGLContext context;
	EGLSurface draw_surface;
	EGLSurface read_surface;
};

struct wlr_eglstream {
	struct wlr_drm_backend *drm;
	struct wlr_egl *egl;
	EGLStreamKHR stream;
	EGLSurface surface;
	bool busy; // true for e.g. modeset in progress
};

struct wlr_egl {
	EGLDisplay display;
	EGLContext context;
	EGLDeviceEXT device; // may be EGL_NO_DEVICE_EXT
	struct gbm_device *gbm_device;
	EGLConfig egl_config; // For setting up EGLStreams context
	struct wlr_eglstream *current_eglstream; // Non-null for EGLStream frame

	struct {
		// Display extensions
		bool bind_wayland_display_wl;
		bool image_base_khr;
		bool image_dma_buf_export_mesa;
		bool image_dmabuf_import_ext;
		bool image_dmabuf_import_modifiers_ext;

		// Device extensions
		bool device_drm_ext;
	} exts;

	struct {
		PFNEGLGETPLATFORMDISPLAYEXTPROC eglGetPlatformDisplayEXT;
		PFNEGLCREATEIMAGEKHRPROC eglCreateImageKHR;
		PFNEGLDESTROYIMAGEKHRPROC eglDestroyImageKHR;
		PFNEGLQUERYWAYLANDBUFFERWL eglQueryWaylandBufferWL;
		PFNEGLBINDWAYLANDDISPLAYWL eglBindWaylandDisplayWL;
		PFNEGLUNBINDWAYLANDDISPLAYWL eglUnbindWaylandDisplayWL;
		PFNEGLQUERYDMABUFFORMATSEXTPROC eglQueryDmaBufFormatsEXT;
		PFNEGLQUERYDMABUFMODIFIERSEXTPROC eglQueryDmaBufModifiersEXT;
		PFNEGLEXPORTDMABUFIMAGEQUERYMESAPROC eglExportDMABUFImageQueryMESA;
		PFNEGLEXPORTDMABUFIMAGEMESAPROC eglExportDMABUFImageMESA;
		PFNEGLDEBUGMESSAGECONTROLKHRPROC eglDebugMessageControlKHR;
		PFNEGLQUERYDISPLAYATTRIBEXTPROC eglQueryDisplayAttribEXT;
		PFNEGLQUERYDEVICESTRINGEXTPROC eglQueryDeviceStringEXT;
		// EGLStreams
		PFNEGLQUERYDEVICESEXTPROC eglQueryDevicesEXT;
		PFNEGLGETOUTPUTLAYERSEXTPROC eglGetOutputLayersEXT;
		PFNEGLCREATESTREAMKHRPROC eglCreateStreamKHR;
		PFNEGLDESTROYSTREAMKHRPROC eglDestroyStreamKHR;
		PFNEGLSTREAMCONSUMEROUTPUTEXTPROC eglStreamConsumerOutputEXT;
		PFNEGLCREATESTREAMPRODUCERSURFACEKHRPROC eglCreateStreamProducerSurfaceKHR;
		PFNEGLSTREAMCONSUMERACQUIREATTRIBNVPROC eglStreamConsumerAcquireAttribNV;
		PFNEGLQUERYSTREAMATTRIBNV eglQueryStreamAttribNV;
		PFNEGLSTREAMCONSUMERRELEASEKHR eglStreamConsumerReleaseKHR;
		PFNEGLQUERYSTREAMKHRPROC eglQueryStreamKHR;
		PFNEGLCREATESTREAMATTRIBNVPROC eglCreateStreamAttribNV;
		PFNEGLSTREAMCONSUMERGLTEXTUREEXTERNALKHRPROC eglStreamConsumerGLTextureExternalKHR;
	} procs;

	struct wl_display *wl_display;

	struct wlr_drm_format_set dmabuf_texture_formats;
	struct wlr_drm_format_set dmabuf_render_formats;
};

/**
 * Initializes an EGL context for the given platform and remote display.
 * Will attempt to load all possibly required api functions.
 */
struct wlr_egl *wlr_egl_create(EGLenum platform, void *remote_display);

/**
 * Frees all related EGL resources, makes the context not-current and
 * unbinds a bound wayland display.
 */
void wlr_egl_destroy(struct wlr_egl *egl);

/**
 * Binds the given display to the EGL instance.
 * This will allow clients to create EGL surfaces from wayland ones and render
 * to it.
 */
bool wlr_egl_bind_display(struct wlr_egl *egl, struct wl_display *local_display);

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

bool wlr_egl_export_image_to_dmabuf(struct wlr_egl *egl, EGLImageKHR image,
	int32_t width, int32_t height, uint32_t flags,
	struct wlr_dmabuf_attributes *attribs);

/**
 * Destroys an EGL image created with the given wlr_egl.
 */
bool wlr_egl_destroy_image(struct wlr_egl *egl, EGLImageKHR image);

/**
 * Make the EGL context current.
 *
 * Callers are expected to clear the current context when they are done by
 * calling wlr_egl_unset_current.
 */
bool wlr_egl_make_current(struct wlr_egl *egl);

bool wlr_egl_unset_current(struct wlr_egl *egl);

bool wlr_egl_is_current(struct wlr_egl *egl);

/**
 * Save the current EGL context to the structure provided in the argument.
 *
 * This includes display, context, draw surface and read surface.
 */
void wlr_egl_save_context(struct wlr_egl_context *context);

/**
 * Restore EGL context that was previously saved using wlr_egl_save_current().
 */
bool wlr_egl_restore_context(struct wlr_egl_context *context);

int wlr_egl_dup_drm_fd(struct wlr_egl *egl);

/**
 * Sets up EGLSurface for passed egl_stream
 * Expects egl_stream->drm and egl_stream->egl to be valid
 */
bool wlr_egl_create_eglstreams_surface(struct wlr_eglstream *egl_stream,
		uint32_t plane_id, int width, int height);

/**
 * Destroys egl_stream and frees resources used
 */
void wlr_egl_destroy_eglstreams_surface(struct wlr_eglstream *egl_stream);

/**
 * Flips EGLStream for presentation. Updated buffers ages.
 * Expects EGLStream surface to be current
 */
struct wlr_output;
bool wlr_egl_flip_eglstreams_page(struct wlr_output *output);

/**
 * Flips source transform around X axis to match EGL co-ordinate system.
 */
enum wl_output_transform
wlr_egl_normalize_output_transform(enum wl_output_transform source_transform);

/**
 * Load and initialized nvidia eglstream controller.
 * for mapping client EGL surfaces.
 */
void init_eglstream_controller(struct wl_display *display);

#endif
