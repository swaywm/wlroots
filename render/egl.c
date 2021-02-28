#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <drm_fourcc.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <gbm.h>
#include <wlr/render/egl.h>
#include <wlr/util/log.h>
#include <wlr/util/region.h>
#include <xf86drm.h>
#include <dlfcn.h>
#include <wlr/types/wlr_surface.h>
#include "backend/drm/drm.h"
#include "render/swapchain.h"
#include "render/eglstreams_allocator.h"
#include "wayland-eglstream-controller-protocol.h"

static enum wlr_log_importance egl_log_importance_to_wlr(EGLint type,
		EGLint error) {
	switch (error) {
		// Do not spam about EGLStream errors
		case EGL_BAD_STATE_KHR:
		case EGL_BAD_STREAM_KHR:
			return WLR_DEBUG;
		default:
			break;
	}

	switch (type) {
	case EGL_DEBUG_MSG_CRITICAL_KHR: return WLR_ERROR;
	case EGL_DEBUG_MSG_ERROR_KHR:    return WLR_ERROR;
	case EGL_DEBUG_MSG_WARN_KHR:     return WLR_ERROR;
	case EGL_DEBUG_MSG_INFO_KHR:     return WLR_INFO;
	default:                         return WLR_INFO;
	}
}

static const char *egl_error_str(EGLint error) {
	switch (error) {
	case EGL_SUCCESS:
		return "EGL_SUCCESS";
	case EGL_NOT_INITIALIZED:
		return "EGL_NOT_INITIALIZED";
	case EGL_BAD_ACCESS:
		return "EGL_BAD_ACCESS";
	case EGL_BAD_ALLOC:
		return "EGL_BAD_ALLOC";
	case EGL_BAD_ATTRIBUTE:
		return "EGL_BAD_ATTRIBUTE";
	case EGL_BAD_CONTEXT:
		return "EGL_BAD_CONTEXT";
	case EGL_BAD_CONFIG:
		return "EGL_BAD_CONFIG";
	case EGL_BAD_CURRENT_SURFACE:
		return "EGL_BAD_CURRENT_SURFACE";
	case EGL_BAD_DISPLAY:
		return "EGL_BAD_DISPLAY";
	case EGL_BAD_DEVICE_EXT:
		return "EGL_BAD_DEVICE_EXT";
	case EGL_BAD_SURFACE:
		return "EGL_BAD_SURFACE";
	case EGL_BAD_MATCH:
		return "EGL_BAD_MATCH";
	case EGL_BAD_PARAMETER:
		return "EGL_BAD_PARAMETER";
	case EGL_BAD_NATIVE_PIXMAP:
		return "EGL_BAD_NATIVE_PIXMAP";
	case EGL_BAD_NATIVE_WINDOW:
		return "EGL_BAD_NATIVE_WINDOW";
	case EGL_CONTEXT_LOST:
		return "EGL_CONTEXT_LOST";
	case EGL_BAD_STREAM_KHR:
		return "EGL_BAD_STREAM_KHR";
	case EGL_BAD_STATE_KHR:
		return "EGL_BAD_STATE_KHR";
	}
	return "unknown error";
}

static void egl_log(EGLenum error, const char *command, EGLint msg_type,
		EGLLabelKHR thread, EGLLabelKHR obj, const char *msg) {
	_wlr_log(egl_log_importance_to_wlr(msg_type, error),
		"[EGL] command: %s, error: %s (0x%x), message: \"%s\"",
		command, egl_error_str(error), error, msg);
}

static bool check_egl_ext(const char *exts, const char *ext) {
	size_t extlen = strlen(ext);
	const char *end = exts + strlen(exts);

	while (exts < end) {
		if (*exts == ' ') {
			exts++;
			continue;
		}
		size_t n = strcspn(exts, " ");
		if (n == extlen && strncmp(ext, exts, n) == 0) {
			return true;
		}
		exts += n;
	}
	return false;
}

static void load_egl_proc(void *proc_ptr, const char *name) {
	void *proc = (void *)eglGetProcAddress(name);
	if (proc == NULL) {
		wlr_log(WLR_ERROR, "eglGetProcAddress(%s) failed", name);
		abort();
	}
	*(void **)proc_ptr = proc;
}

static int get_egl_dmabuf_formats(struct wlr_egl *egl, int **formats);
static int get_egl_dmabuf_modifiers(struct wlr_egl *egl, int format,
	uint64_t **modifiers, EGLBoolean **external_only);

static void init_dmabuf_formats(struct wlr_egl *egl) {
	int *formats;
	int formats_len = get_egl_dmabuf_formats(egl, &formats);
	if (formats_len < 0) {
		return;
	}

	bool has_modifiers = false;
	for (int i = 0; i < formats_len; i++) {
		uint32_t fmt = formats[i];

		uint64_t *modifiers;
		EGLBoolean *external_only;
		int modifiers_len =
			get_egl_dmabuf_modifiers(egl, fmt, &modifiers, &external_only);
		if (modifiers_len < 0) {
			continue;
		}

		has_modifiers = has_modifiers || modifiers_len > 0;

		if (modifiers_len == 0) {
			wlr_drm_format_set_add(&egl->dmabuf_texture_formats, fmt,
				DRM_FORMAT_MOD_INVALID);
			wlr_drm_format_set_add(&egl->dmabuf_render_formats, fmt,
				DRM_FORMAT_MOD_INVALID);
		}

		for (int j = 0; j < modifiers_len; j++) {
			wlr_drm_format_set_add(&egl->dmabuf_texture_formats, fmt,
				modifiers[j]);
			if (!external_only[j]) {
				wlr_drm_format_set_add(&egl->dmabuf_render_formats, fmt,
					modifiers[j]);
			}
		}

		free(modifiers);
		free(external_only);
	}

	char *str_formats = malloc(formats_len * 5 + 1);
	if (str_formats == NULL) {
		goto out;
	}
	for (int i = 0; i < formats_len; i++) {
		snprintf(&str_formats[i*5], (formats_len - i) * 5 + 1, "%.4s ",
			(char*)&formats[i]);
	}
	wlr_log(WLR_DEBUG, "Supported DMA-BUF formats: %s", str_formats);
	wlr_log(WLR_DEBUG, "EGL DMA-BUF format modifiers %s",
		has_modifiers ? "supported" : "unsupported");
	free(str_formats);

out:
	free(formats);
}

struct wlr_egl *wlr_egl_create(EGLenum platform, void *remote_display) {
	struct wlr_egl *egl = calloc(1, sizeof(struct wlr_egl));
	if (egl == NULL) {
		wlr_log_errno(WLR_ERROR, "Allocation failed");
		return NULL;
	}

	const char *client_exts_str = eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);
	if (client_exts_str == NULL) {
		if (eglGetError() == EGL_BAD_DISPLAY) {
			wlr_log(WLR_ERROR, "EGL_EXT_client_extensions not supported");
		} else {
			wlr_log(WLR_ERROR, "Failed to query EGL client extensions");
		}
		return NULL;
	}

	if (!check_egl_ext(client_exts_str, "EGL_EXT_platform_base")) {
		wlr_log(WLR_ERROR, "EGL_EXT_platform_base not supported");
		return NULL;
	}
	load_egl_proc(&egl->procs.eglGetPlatformDisplayEXT,
		"eglGetPlatformDisplayEXT");

	if (check_egl_ext(client_exts_str, "EGL_KHR_debug")) {
		load_egl_proc(&egl->procs.eglDebugMessageControlKHR,
			"eglDebugMessageControlKHR");

		static const EGLAttrib debug_attribs[] = {
			EGL_DEBUG_MSG_CRITICAL_KHR, EGL_TRUE,
			EGL_DEBUG_MSG_ERROR_KHR, EGL_TRUE,
			EGL_DEBUG_MSG_WARN_KHR, EGL_TRUE,
			EGL_DEBUG_MSG_INFO_KHR, EGL_TRUE,
			EGL_NONE,
		};
		egl->procs.eglDebugMessageControlKHR(egl_log, debug_attribs);
	}

	if (eglBindAPI(EGL_OPENGL_ES_API) == EGL_FALSE) {
		wlr_log(WLR_ERROR, "Failed to bind to the OpenGL ES API");
		goto error;
	}

	EGLint *attribsDisplay = NULL;

	// Is EGLStreams mode all dmabuf-related functiobality must be disabled.
	// Else native clients may fail to start.
	bool is_eglstreams = false;
	if (platform == EGL_PLATFORM_DEVICE_EXT) {
		is_eglstreams = true;
		int drm_fd = (int)(long)remote_display;
		remote_display = NULL;
		const char *extensions = eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);
		if (!check_egl_ext(extensions, "EGL_EXT_device_base") &&
			(!check_egl_ext(extensions, "EGL_EXT_device_enumeration") ||
			!check_egl_ext(extensions, "EGL_EXT_device_query"))) {
			wlr_log(WLR_ERROR, "Failed to query needed EGL extensions"
					" for EGL_PLATFORM_DEVICE_EXT");
			goto error;
		}


		load_egl_proc(&egl->procs.eglQueryDeviceStringEXT,
				"eglQueryDeviceStringEXT");
		load_egl_proc(&egl->procs.eglQueryDevicesEXT,
				"eglQueryDevicesEXT");
		load_egl_proc(&egl->procs.eglGetOutputLayersEXT,
				"eglGetOutputLayersEXT");
		load_egl_proc(&egl->procs.eglCreateStreamKHR,
				"eglCreateStreamKHR");
		load_egl_proc(&egl->procs.eglDestroyStreamKHR,
				"eglDestroyStreamKHR");
		load_egl_proc(&egl->procs.eglStreamConsumerOutputEXT,
				"eglStreamConsumerOutputEXT");
		load_egl_proc(&egl->procs.eglCreateStreamProducerSurfaceKHR,
				"eglCreateStreamProducerSurfaceKHR");
		load_egl_proc(&egl->procs.eglStreamConsumerAcquireAttribNV,
				"eglStreamConsumerAcquireAttribNV");
		load_egl_proc(&egl->procs.eglQueryStreamAttribNV,
				"eglQueryStreamAttribNV");
		load_egl_proc(&egl->procs.eglStreamConsumerReleaseKHR,
				"eglStreamConsumerReleaseKHR");
		load_egl_proc(&egl->procs.eglQueryStreamKHR,
				"eglQueryStreamKHR");
		load_egl_proc(&egl->procs.eglCreateStreamAttribNV,
				"eglCreateStreamAttribNV");
		load_egl_proc(&egl->procs.eglStreamConsumerGLTextureExternalKHR,
				"eglStreamConsumerGLTextureExternalKHR");

		EGLint num_devices;
		if (!egl->procs.eglQueryDevicesEXT(0, NULL, &num_devices) || num_devices < 1) {
			wlr_log(WLR_ERROR, "No devices found for EGL_PLATFORM_DEVICE_EXT");
			goto error;
		}

		num_devices = num_devices > 255 ? 255 : num_devices;

		EGLDeviceEXT devices[255];
		if (!egl->procs.eglQueryDevicesEXT(num_devices, devices, &num_devices)) {
			wlr_log(WLR_ERROR, "Failed get EGLDevice pointers"
					" for EGL_PLATFORM_DEVICE_EXT");
			goto error;
		}

		const char *drmDeviceFile = drmGetDeviceNameFromFd2(drm_fd);

		for (int i = 0; i < num_devices; i++) {
			EGLDeviceEXT device = devices[i];
			const char *device_extensions =
				egl->procs.eglQueryDeviceStringEXT(device, EGL_EXTENSIONS);
			if (check_egl_ext(device_extensions, "EGL_EXT_device_drm") &&
					device != EGL_NO_DEVICE_EXT) {
				const char *currentDeviceFile =
					egl->procs.eglQueryDeviceStringEXT(device, EGL_DRM_DEVICE_FILE_EXT);
				if (strcmp(drmDeviceFile, currentDeviceFile) == 0) {
					remote_display = device;
					break;
				}
			}
		}

		if (remote_display == NULL) {
			wlr_log(WLR_ERROR, "Can't get EGLDevice for drm device %s "
					"to setup EGLStreams mode", drmDeviceFile);
			goto error;
		}
		attribsDisplay = calloc(3, sizeof(EGLint));
		attribsDisplay[0] = EGL_DRM_MASTER_FD_EXT;
		attribsDisplay[1] = drm_fd;
		attribsDisplay[2] = EGL_NONE;
	}

	egl->display = egl->procs.eglGetPlatformDisplayEXT(platform,
		remote_display, attribsDisplay);
	free(attribsDisplay);

	if (egl->display == EGL_NO_DISPLAY) {
		wlr_log(WLR_ERROR, "Failed to create EGL display");
		goto error;
	}

	EGLint major, minor;
	if (eglInitialize(egl->display, &major, &minor) == EGL_FALSE) {
		wlr_log(WLR_ERROR, "Failed to initialize EGL");
		goto error;
	}

	const char *display_exts_str = eglQueryString(egl->display, EGL_EXTENSIONS);
	if (display_exts_str == NULL) {
		wlr_log(WLR_ERROR, "Failed to query EGL display extensions");
		return NULL;
	}

	if (check_egl_ext(display_exts_str, "EGL_KHR_image_base")) {
		egl->exts.image_base_khr = true;
		load_egl_proc(&egl->procs.eglCreateImageKHR, "eglCreateImageKHR");
		load_egl_proc(&egl->procs.eglDestroyImageKHR, "eglDestroyImageKHR");
	}

	// Disable all dmf-buf functionality for EGLStreams.
	// TODO: Enable when nvidia driver is ready.
	if (!is_eglstreams) {
		egl->exts.image_dmabuf_import_ext = 
			check_egl_ext(display_exts_str, "EGL_EXT_image_dma_buf_import");
		if (check_egl_ext(display_exts_str,
				"EGL_EXT_image_dma_buf_import_modifiers")) {
			egl->exts.image_dmabuf_import_modifiers_ext = true;
			load_egl_proc(&egl->procs.eglQueryDmaBufFormatsEXT,
				"eglQueryDmaBufFormatsEXT");
			load_egl_proc(&egl->procs.eglQueryDmaBufModifiersEXT,
				"eglQueryDmaBufModifiersEXT");
		}

		if (check_egl_ext(display_exts_str, "EGL_MESA_image_dma_buf_export")) {
			egl->exts.image_dma_buf_export_mesa = true;
			load_egl_proc(&egl->procs.eglExportDMABUFImageQueryMESA,
				"eglExportDMABUFImageQueryMESA");
			load_egl_proc(&egl->procs.eglExportDMABUFImageMESA,
				"eglExportDMABUFImageMESA");
		}
	}

	if (check_egl_ext(display_exts_str, "EGL_WL_bind_wayland_display")) {
		egl->exts.bind_wayland_display_wl = true;
		load_egl_proc(&egl->procs.eglBindWaylandDisplayWL,
			"eglBindWaylandDisplayWL");
		load_egl_proc(&egl->procs.eglUnbindWaylandDisplayWL,
			"eglUnbindWaylandDisplayWL");
		load_egl_proc(&egl->procs.eglQueryWaylandBufferWL,
			"eglQueryWaylandBufferWL");
	}

	egl->egl_config = EGL_NO_CONFIG_KHR;
	if (is_eglstreams) {
		if (!check_egl_ext(display_exts_str, "EGL_EXT_output_base") ||
			!check_egl_ext(display_exts_str,
				"EGL_EXT_output_drm") ||
			!check_egl_ext(display_exts_str,
				"EGL_KHR_stream") ||
			!check_egl_ext(display_exts_str,
				"EGL_KHR_stream_producer_eglsurface") ||
			!check_egl_ext(display_exts_str,
				"EGL_EXT_stream_consumer_egloutput") ||
			!check_egl_ext(display_exts_str,
				"EGL_NV_stream_attrib") ||
			!check_egl_ext(display_exts_str,
				"EGL_EXT_stream_acquire_mode") ||
			!check_egl_ext(display_exts_str,
				"EGL_KHR_stream_consumer_gltexture") ||
			!check_egl_ext(display_exts_str,
				"EGL_WL_wayland_eglstream")) {
			wlr_log(WLR_ERROR, "Some required display extensions for "
					"EGLStreams are missing");
			goto error;

		}
		EGLint config_attribs [] = {
			EGL_SURFACE_TYPE,         EGL_STREAM_BIT_KHR,
			EGL_RED_SIZE,             1,
			EGL_GREEN_SIZE,           1,
			EGL_BLUE_SIZE,            1,
			EGL_RENDERABLE_TYPE,      EGL_OPENGL_ES2_BIT,
			EGL_CONFIG_CAVEAT,        EGL_NONE,
			EGL_NONE,
		};

		EGLint egl_num_configs;
		if (!eglChooseConfig(egl->display, config_attribs,
			&egl->egl_config, 1, &egl_num_configs) ||
				egl_num_configs < 1) {
			wlr_log(WLR_ERROR,
				"No EGL configs foung for EGLStreams setup");
			goto error;
		}
	}

	const char *device_exts_str = NULL;
	if (check_egl_ext(client_exts_str, "EGL_EXT_device_query")) {
		load_egl_proc(&egl->procs.eglQueryDisplayAttribEXT,
			"eglQueryDisplayAttribEXT");
		load_egl_proc(&egl->procs.eglQueryDeviceStringEXT,
			"eglQueryDeviceStringEXT");

		EGLAttrib device_attrib;
		if (!egl->procs.eglQueryDisplayAttribEXT(egl->display,
				EGL_DEVICE_EXT, &device_attrib)) {
			wlr_log(WLR_ERROR, "eglQueryDisplayAttribEXT(EGL_DEVICE_EXT) failed");
			goto error;
		}
		EGLDeviceEXT displayDevice = (EGLDeviceEXT)device_attrib;

		if(egl->device && egl->device != displayDevice) {
			wlr_log(WLR_ERROR,
				"Queried EGL display device is different "
				"from one display was created from");
			goto error;
		}

		egl->device = displayDevice; 

		device_exts_str =
			egl->procs.eglQueryDeviceStringEXT(egl->device, EGL_EXTENSIONS);
		if (device_exts_str == NULL) {
			wlr_log(WLR_ERROR, "eglQueryDeviceStringEXT(EGL_EXTENSIONS) failed");
			goto error;
		}

		if (check_egl_ext(device_exts_str, "EGL_MESA_device_software")) {
			const char *allow_software = getenv("WLR_RENDERER_ALLOW_SOFTWARE");
			if (allow_software != NULL && strcmp(allow_software, "1") == 0) {
				wlr_log(WLR_INFO, "Using software rendering");
			} else {
				wlr_log(WLR_ERROR, "Software rendering detected, please use "
						"the WLR_RENDERER_ALLOW_SOFTWARE environment variable "
						"to proceed");
				goto error;
			}
		}

		egl->exts.device_drm_ext =
			check_egl_ext(device_exts_str, "EGL_EXT_device_drm");
	}

	if (!check_egl_ext(display_exts_str, "EGL_KHR_no_config_context") &&
			!check_egl_ext(display_exts_str, "EGL_MESA_configless_context")) {
		wlr_log(WLR_ERROR, "EGL_KHR_no_config_context or "
			"EGL_MESA_configless_context not supported");
		goto error;
	}

	if (!check_egl_ext(display_exts_str, "EGL_KHR_surfaceless_context")) {
		wlr_log(WLR_ERROR, 
			"EGL_KHR_surfaceless_context not supported");
		goto error;
	}

	wlr_log(WLR_INFO, "Using EGL %d.%d", (int)major, (int)minor);
	wlr_log(WLR_INFO, "Supported EGL client extensions: %s", client_exts_str);
	wlr_log(WLR_INFO, "Supported EGL display extensions: %s", display_exts_str);
	if (device_exts_str != NULL) {
		wlr_log(WLR_INFO, "Supported EGL device extensions: %s", device_exts_str);
	}
	wlr_log(WLR_INFO, "EGL vendor: %s", eglQueryString(egl->display, EGL_VENDOR));

	init_dmabuf_formats(egl);

	bool ext_context_priority =
		check_egl_ext(display_exts_str, "EGL_IMG_context_priority");

	size_t atti = 0;
	EGLint attribs[5];
	attribs[atti++] = EGL_CONTEXT_CLIENT_VERSION;
	attribs[atti++] = 2;

	// On DRM, request a high priority context if possible
	bool request_high_priority = ext_context_priority &&
		(platform == EGL_PLATFORM_GBM_MESA || platform == EGL_PLATFORM_DEVICE_EXT);

	// Try to reschedule all of our rendering to be completed first. If it
	// fails, it will fallback to the default priority (MEDIUM).
	if (request_high_priority) {
		attribs[atti++] = EGL_CONTEXT_PRIORITY_LEVEL_IMG;
		attribs[atti++] = EGL_CONTEXT_PRIORITY_HIGH_IMG;
	}

	attribs[atti++] = EGL_NONE;
	assert(atti <= sizeof(attribs)/sizeof(attribs[0]));

	egl->context = eglCreateContext(egl->display, egl->egl_config,
		EGL_NO_CONTEXT, attribs);
	if (egl->context == EGL_NO_CONTEXT) {
		wlr_log(WLR_ERROR, "Failed to create EGL context");
		goto error;
	}

	if (request_high_priority) {
		EGLint priority = EGL_CONTEXT_PRIORITY_MEDIUM_IMG;
		eglQueryContext(egl->display, egl->context,
			EGL_CONTEXT_PRIORITY_LEVEL_IMG, &priority);
		if (priority != EGL_CONTEXT_PRIORITY_HIGH_IMG) {
			wlr_log(WLR_INFO, "Failed to obtain a high priority context");
		} else {
			wlr_log(WLR_DEBUG, "Obtained high priority context");
		}
	}

	return egl;

error:
	eglMakeCurrent(EGL_NO_DISPLAY, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
	if (egl->display) {
		eglTerminate(egl->display);
	}
	eglReleaseThread();
	return NULL;
}

void wlr_egl_destroy(struct wlr_egl *egl) {
	if (egl == NULL) {
		return;
	}

	wlr_drm_format_set_finish(&egl->dmabuf_render_formats);
	wlr_drm_format_set_finish(&egl->dmabuf_texture_formats);

	eglMakeCurrent(egl->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
	if (egl->wl_display) {
		assert(egl->exts.bind_wayland_display_wl);
		egl->procs.eglUnbindWaylandDisplayWL(egl->display, egl->wl_display);
	}

	eglDestroyContext(egl->display, egl->context);
	eglTerminate(egl->display);
	eglReleaseThread();

	if (egl->gbm_device) {
		gbm_device_destroy(egl->gbm_device);
	}

	free(egl);
}

bool wlr_egl_bind_display(struct wlr_egl *egl, struct wl_display *local_display) {
	if (!egl->exts.bind_wayland_display_wl) {
		return false;
	}

	if (egl->procs.eglBindWaylandDisplayWL(egl->display, local_display)) {
		egl->wl_display = local_display;
		return true;
	}

	return false;
}

bool wlr_egl_destroy_image(struct wlr_egl *egl, EGLImage image) {
	if (!egl->exts.image_base_khr) {
		return false;
	}
	if (!image) {
		return true;
	}
	return egl->procs.eglDestroyImageKHR(egl->display, image);
}

bool wlr_egl_make_current(struct wlr_egl *egl) {
	EGLSurface surface = egl->current_eglstream ?
		egl->current_eglstream->surface : EGL_NO_SURFACE;

	if (!eglMakeCurrent(egl->display, surface, surface, egl->context)) {
		wlr_log(WLR_ERROR, "eglMakeCurrent failed");
		return false;
	}
	return true;
}

bool wlr_egl_unset_current(struct wlr_egl *egl) {
	egl->current_eglstream = NULL;
	if (!eglMakeCurrent(egl->display, EGL_NO_SURFACE, EGL_NO_SURFACE,
			EGL_NO_CONTEXT)) {
		wlr_log(WLR_ERROR, "eglMakeCurrent failed");
		return false;
	}
	return true;
}

bool wlr_egl_is_current(struct wlr_egl *egl) {
	return eglGetCurrentContext() == egl->context;
}

void wlr_egl_save_context(struct wlr_egl_context *context) {
	context->display = eglGetCurrentDisplay();
	context->context = eglGetCurrentContext();
	context->draw_surface = eglGetCurrentSurface(EGL_DRAW);
	context->read_surface = eglGetCurrentSurface(EGL_READ);
}

bool wlr_egl_restore_context(struct wlr_egl_context *context) {
	// If the saved context is a null-context, we must use the current
	// display instead of the saved display because eglMakeCurrent() can't
	// handle EGL_NO_DISPLAY.
	EGLDisplay display = context->display == EGL_NO_DISPLAY ?
		eglGetCurrentDisplay() : context->display;

	// If the current display is also EGL_NO_DISPLAY, we assume that there
	// is currently no context set and no action needs to be taken to unset
	// the context.
	if (display == EGL_NO_DISPLAY) {
		return true;
	}

	return eglMakeCurrent(display, context->draw_surface,
			context->read_surface, context->context);
}

EGLImageKHR wlr_egl_create_image_from_wl_drm(struct wlr_egl *egl,
		struct wl_resource *data, EGLint *fmt, int *width, int *height,
		bool *inverted_y) {
	if (!egl->exts.bind_wayland_display_wl || !egl->exts.image_base_khr) {
		return NULL;
	}

	if (!egl->procs.eglQueryWaylandBufferWL(egl->display, data,
			EGL_TEXTURE_FORMAT, fmt)) {
		return NULL;
	}

	egl->procs.eglQueryWaylandBufferWL(egl->display, data, EGL_WIDTH, width);
	egl->procs.eglQueryWaylandBufferWL(egl->display, data, EGL_HEIGHT, height);

	EGLint _inverted_y;
	if (egl->procs.eglQueryWaylandBufferWL(egl->display, data,
			EGL_WAYLAND_Y_INVERTED_WL, &_inverted_y)) {
		*inverted_y = !!_inverted_y;
	} else {
		*inverted_y = false;
	}

	const EGLint attribs[] = {
		EGL_WAYLAND_PLANE_WL, 0,
		EGL_NONE,
	};
	return egl->procs.eglCreateImageKHR(egl->display, egl->context,
		EGL_WAYLAND_BUFFER_WL, data, attribs);
}

EGLImageKHR wlr_egl_create_image_from_dmabuf(struct wlr_egl *egl,
		struct wlr_dmabuf_attributes *attributes, bool *external_only) {
	if (!egl->exts.image_base_khr || !egl->exts.image_dmabuf_import_ext) {
		wlr_log(WLR_ERROR, "dmabuf import extension not present");
		return NULL;
	}

	bool has_modifier = false;

	// we assume the same way we assumed formats without the import_modifiers
	// extension that mod_linear is supported. The special mod mod_invalid
	// is sometimes used to signal modifier unawareness which is what we
	// have here
	if (attributes->modifier != DRM_FORMAT_MOD_INVALID &&
			attributes->modifier != DRM_FORMAT_MOD_LINEAR) {
		if (!egl->exts.image_dmabuf_import_modifiers_ext) {
			wlr_log(WLR_ERROR, "dmabuf modifiers extension not present");
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
	} attr_names[WLR_DMABUF_MAX_PLANES] = {
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
			attribs[atti++] = attributes->modifier & 0xFFFFFFFF;
			attribs[atti++] = attr_names[i].mod_hi;
			attribs[atti++] = attributes->modifier >> 32;
		}
	}
	attribs[atti++] = EGL_NONE;
	assert(atti < sizeof(attribs)/sizeof(attribs[0]));

	EGLImageKHR image = egl->procs.eglCreateImageKHR(egl->display, EGL_NO_CONTEXT,
		EGL_LINUX_DMA_BUF_EXT, NULL, attribs);
	if (image == EGL_NO_IMAGE_KHR) {
		wlr_log(WLR_ERROR, "eglCreateImageKHR failed");
		return EGL_NO_IMAGE_KHR;
	}

	*external_only = !wlr_drm_format_set_has(&egl->dmabuf_render_formats,
		attributes->format, attributes->modifier);
	return image;
}

static int get_egl_dmabuf_formats(struct wlr_egl *egl, int **formats) {
	if (!egl->exts.image_dmabuf_import_ext) {
		wlr_log(WLR_DEBUG, "DMA-BUF import extension not present");
		return -1;
	}

	// when we only have the image_dmabuf_import extension we can't query
	// which formats are supported. These two are on almost always
	// supported; it's the intended way to just try to create buffers.
	// Just a guess but better than not supporting dmabufs at all,
	// given that the modifiers extension isn't supported everywhere.
	if (!egl->exts.image_dmabuf_import_modifiers_ext) {
		static const int fallback_formats[] = {
			DRM_FORMAT_ARGB8888,
			DRM_FORMAT_XRGB8888,
		};
		static unsigned num = sizeof(fallback_formats) /
			sizeof(fallback_formats[0]);

		*formats = calloc(num, sizeof(int));
		if (!*formats) {
			wlr_log_errno(WLR_ERROR, "Allocation failed");
			return -1;
		}

		memcpy(*formats, fallback_formats, num * sizeof(**formats));
		return num;
	}

	EGLint num;
	if (!egl->procs.eglQueryDmaBufFormatsEXT(egl->display, 0, NULL, &num)) {
		wlr_log(WLR_ERROR, "Failed to query number of dmabuf formats");
		return -1;
	}

	*formats = calloc(num, sizeof(int));
	if (*formats == NULL) {
		wlr_log(WLR_ERROR, "Allocation failed: %s", strerror(errno));
		return -1;
	}

	if (!egl->procs.eglQueryDmaBufFormatsEXT(egl->display, num, *formats, &num)) {
		wlr_log(WLR_ERROR, "Failed to query dmabuf format");
		free(*formats);
		return -1;
	}
	return num;
}

static int get_egl_dmabuf_modifiers(struct wlr_egl *egl, int format,
		uint64_t **modifiers, EGLBoolean **external_only) {
	*modifiers = NULL;
	*external_only = NULL;

	if (!egl->exts.image_dmabuf_import_ext) {
		wlr_log(WLR_DEBUG, "DMA-BUF extension not present");
		return -1;
	}
	if (!egl->exts.image_dmabuf_import_modifiers_ext) {
		return 0;
	}

	EGLint num;
	if (!egl->procs.eglQueryDmaBufModifiersEXT(egl->display, format, 0,
			NULL, NULL, &num)) {
		wlr_log(WLR_ERROR, "Failed to query dmabuf number of modifiers");
		return -1;
	}
	if (num == 0) {
		return 0;
	}

	*modifiers = calloc(num, sizeof(uint64_t));
	if (*modifiers == NULL) {
		wlr_log_errno(WLR_ERROR, "Allocation failed");
		return -1;
	}
	*external_only = calloc(num, sizeof(EGLBoolean));
	if (*external_only == NULL) {
		wlr_log_errno(WLR_ERROR, "Allocation failed");
		free(*modifiers);
		*modifiers = NULL;
		return -1;
	}

	if (!egl->procs.eglQueryDmaBufModifiersEXT(egl->display, format, num,
			*modifiers, *external_only, &num)) {
		wlr_log(WLR_ERROR, "Failed to query dmabuf modifiers");
		free(*modifiers);
		free(*external_only);
		return -1;
	}
	return num;
}

const struct wlr_drm_format_set *wlr_egl_get_dmabuf_texture_formats(
		struct wlr_egl *egl) {
	return &egl->dmabuf_texture_formats;
}

const struct wlr_drm_format_set *wlr_egl_get_dmabuf_render_formats(
		struct wlr_egl *egl) {
	return &egl->dmabuf_render_formats;
}

bool wlr_egl_export_image_to_dmabuf(struct wlr_egl *egl, EGLImageKHR image,
		int32_t width, int32_t height, uint32_t flags,
		struct wlr_dmabuf_attributes *attribs) {
	memset(attribs, 0, sizeof(struct wlr_dmabuf_attributes));

	if (!egl->exts.image_dma_buf_export_mesa) {
		return false;
	}

	// Only one set of modifiers is returned for all planes
	if (!egl->procs.eglExportDMABUFImageQueryMESA(egl->display, image,
			(int *)&attribs->format, &attribs->n_planes, &attribs->modifier)) {
		return false;
	}
	if (attribs->n_planes > WLR_DMABUF_MAX_PLANES) {
		wlr_log(WLR_ERROR, "EGL returned %d planes, but only %d are supported",
			attribs->n_planes, WLR_DMABUF_MAX_PLANES);
		return false;
	}

	if (!egl->procs.eglExportDMABUFImageMESA(egl->display, image, attribs->fd,
			(EGLint *)attribs->stride, (EGLint *)attribs->offset)) {
		return false;
	}

	attribs->width = width;
	attribs->height = height;
	attribs->flags = flags;
	return true;
}

static bool device_has_name(const drmDevice *device, const char *name) {
	for (size_t i = 0; i < DRM_NODE_MAX; i++) {
		if (!(device->available_nodes & (1 << i))) {
			continue;
		}
		if (strcmp(device->nodes[i], name) == 0) {
			return true;
		}
	}
	return false;
}

static char *get_render_name(const char *name) {
	uint32_t flags = 0;
	int devices_len = drmGetDevices2(flags, NULL, 0);
	if (devices_len < 0) {
		wlr_log(WLR_ERROR, "drmGetDevices2 failed");
		return NULL;
	}
	drmDevice **devices = calloc(devices_len, sizeof(drmDevice *));
	if (devices == NULL) {
		wlr_log_errno(WLR_ERROR, "Allocation failed");
		return NULL;
	}
	devices_len = drmGetDevices2(flags, devices, devices_len);
	if (devices_len < 0) {
		free(devices);
		wlr_log(WLR_ERROR, "drmGetDevices2 failed");
		return NULL;
	}

	const drmDevice *match = NULL;
	for (int i = 0; i < devices_len; i++) {
		if (device_has_name(devices[i], name)) {
			match = devices[i];
			break;
		}
	}

	char *render_name = NULL;
	if (match == NULL) {
		wlr_log(WLR_ERROR, "Cannot find DRM device %s", name);
	} else if (!(match->available_nodes & (1 << DRM_NODE_RENDER))) {
		wlr_log(WLR_ERROR, "DRM device %s has no render node", name);
	} else {
		render_name = strdup(match->nodes[DRM_NODE_RENDER]);
	}

	for (int i = 0; i < devices_len; i++) {
		drmFreeDevice(&devices[i]);
	}
	free(devices);

	return render_name;
}

int wlr_egl_dup_drm_fd(struct wlr_egl *egl) {
	if (egl->device == EGL_NO_DEVICE_EXT || !egl->exts.device_drm_ext) {
		return -1;
	}

	const char *primary_name = egl->procs.eglQueryDeviceStringEXT(egl->device,
		EGL_DRM_DEVICE_FILE_EXT);
	if (primary_name == NULL) {
		wlr_log(WLR_ERROR,
			"eglQueryDeviceStringEXT(EGL_DRM_DEVICE_FILE_EXT) failed");
		return -1;
	}

	char *render_name = get_render_name(primary_name);
	if (render_name == NULL) {
		wlr_log(WLR_ERROR, "Can't find render node name for device %s",
			primary_name);
		return -1;
	}

	int render_fd = open(render_name, O_RDWR | O_NONBLOCK | O_CLOEXEC);
	if (render_fd < 0) {
		wlr_log_errno(WLR_ERROR, "Failed to open DRM render node %s",
			render_name);
		free(render_name);
		return -1;
	}
	free(render_name);

	return render_fd;
}

bool wlr_egl_create_eglstreams_surface(struct wlr_eglstream *egl_stream,
		uint32_t plane_id, int width, int height) {

	EGLAttrib layer_attribs[] = {
		EGL_DRM_PLANE_EXT,
		plane_id,
		EGL_NONE,
	};

	EGLint stream_attribs[] = {
		// Mailbox mode - presenting the most recent frame.
		EGL_STREAM_FIFO_LENGTH_KHR, 0,
		EGL_CONSUMER_AUTO_ACQUIRE_EXT, EGL_FALSE,
		EGL_NONE
	};

	EGLint surface_attribs[] = {
		EGL_WIDTH, width,
		EGL_HEIGHT, height,
		EGL_NONE
	};

	EGLOutputLayerEXT egl_layer;
	EGLint n_layers = 0;
	struct wlr_egl *egl = egl_stream->egl;
	if (!egl) {
		return false;
	}

	EGLBoolean res = egl->procs.eglGetOutputLayersEXT(egl->display,
				layer_attribs, &egl_layer, 1, &n_layers);
	if (!res || !n_layers) {
		wlr_log(WLR_ERROR, "Error getting egl output layer for plane %d", plane_id);
		return false;
	}

	egl_stream->stream = NULL;
	egl_stream->surface = NULL;
	egl_stream->busy = false;

	egl_stream->stream = egl->procs.eglCreateStreamKHR(egl->display, stream_attribs);
    
	if (egl_stream->stream == EGL_NO_STREAM_KHR) {
		wlr_log(WLR_ERROR, "Unable to create egl stream");
		goto error;
	}

	if (!egl->procs.eglStreamConsumerOutputEXT(egl->display, egl_stream->stream, egl_layer)) {
		wlr_log(WLR_ERROR, "Unable to create egl stream consumer");
		goto error;
	}

	egl_stream->surface = egl->procs.eglCreateStreamProducerSurfaceKHR(egl->display,
			egl->egl_config, egl_stream->stream, surface_attribs);

	if (!egl_stream->surface) {
		wlr_log(WLR_ERROR, "Failed to create egl stream producer surface");
		goto error;
	}

	wlr_log(WLR_INFO, "EGLStream for plane %u (%dx%d) has been set up", plane_id,
		width, height);

	return true;

error:
	if (egl_stream->stream) {
		egl->procs.eglDestroyStreamKHR(egl->display, egl_stream->stream);
	}
	if (egl_stream->surface) {
		eglDestroySurface(egl->display, egl_stream->surface);
	}

	return false;
}

void wlr_egl_destroy_eglstreams_surface(struct wlr_eglstream *egl_stream) {
	if (egl_stream->surface) {
		eglDestroySurface(egl_stream->egl->display, egl_stream->surface);
		egl_stream->surface = NULL;
	}
	if (egl_stream->stream) {
		egl_stream->egl->procs.eglDestroyStreamKHR(egl_stream->egl->display,
			egl_stream->stream);
		egl_stream->stream = NULL;
	}
}

bool wlr_egl_flip_eglstreams_page(struct wlr_output *output) {
	assert(wlr_output_is_drm(output));
	struct wlr_drm_connector *conn = (struct wlr_drm_connector *)output;
	assert(conn);
	if (!conn) {
		return false;
	}
	struct wlr_drm_backend *drm = conn->backend;

	struct wlr_drm_crtc *crtc = conn->crtc;
	assert(crtc);
	if (!crtc) {
		return false;
	}
	struct wlr_drm_plane *plane = crtc->primary;
	assert(plane);
	if (!plane) {
		return false;
	}
	struct wlr_drm_surface *surf = drm->parent ?
		&plane->mgpu_surf : &plane->surf;
	struct wlr_swapchain *swapchain = surf->swapchain; 
	assert(swapchain);
	if (!swapchain) {
		return false;
	}
	struct wlr_eglstream_plane *egl_stream_plane = 
		wlr_eglstream_plane_for_id(swapchain->allocator, plane->id);
	assert(egl_stream_plane);
	if (!egl_stream_plane) {
		return false;
	}
	struct wlr_eglstream *egl_stream = &egl_stream_plane->stream;
	struct wlr_egl *egl = egl_stream->egl;

	struct wlr_egl_context old_ctx;
	wlr_egl_save_context(&old_ctx);
	eglMakeCurrent(egl->display, egl_stream->surface, egl_stream->surface, egl->context);

	// Update buffer age.
	// Note: this is needed for wlr damage tracking to work properly.
	// Swapchain's implementation is skipped for EGLStreams buffers.
	EGLint buffer_age;
	if (eglQuerySurface(egl->display, egl_stream->surface,
		EGL_BUFFER_AGE_KHR, &buffer_age) != EGL_TRUE) {
		wlr_log(WLR_ERROR, "EGLstream buffer age couldn't be queried!"
				"Full frame area will be updated!");
		buffer_age = 0;
	}
	// Every buffer is EGLStreams mode is just a wrapper
	// around the only one real EGLStream. 
	// Swapchain works inside EGL.
	for (size_t i = 0; i < WLR_SWAPCHAIN_CAP; i++) {
		struct wlr_swapchain_slot *slot = &swapchain->slots[i];
		if (slot->buffer) {
			slot->age = buffer_age;
		}
	}

	// My experiments show that nvidia driver uses some kind of fast path
	// for damage/buffer age tracking, thus making eglSwapBuffersWithDamage and
	// eglSetDamageRegion useless.
	// Tip: Do not swap if stream is marked busy to avoid deadlock.
	if (!egl_stream->busy && eglSwapBuffers(egl->display, egl_stream->surface) != EGL_TRUE) {
		wlr_log(WLR_ERROR, "Swap buffers for EGLStream failed");
	}


	EGLAttrib acquire_attribs[] = {
		EGL_DRM_FLIP_EVENT_DATA_NV, (EGLAttrib)egl_stream->drm,
		EGL_NONE
	};
	EGLBoolean ok = egl->procs.eglStreamConsumerAcquireAttribNV(egl->display,
			egl_stream->stream, acquire_attribs);

	egl_stream->busy = ok == EGL_FALSE && eglGetError() == EGL_RESOURCE_BUSY_EXT;

	wlr_egl_restore_context(&old_ctx);

	return ok == EGL_TRUE || egl_stream->busy;

}

enum wl_output_transform
wlr_egl_normalize_output_transform(enum wl_output_transform source_transform) {
	enum wl_output_transform result_transform = source_transform;
	switch (source_transform) {
		case WL_OUTPUT_TRANSFORM_NORMAL:
			result_transform = WL_OUTPUT_TRANSFORM_FLIPPED_180;
			break;
		case WL_OUTPUT_TRANSFORM_90:
			result_transform = WL_OUTPUT_TRANSFORM_FLIPPED_90;
			break;
		case WL_OUTPUT_TRANSFORM_180:
			result_transform = WL_OUTPUT_TRANSFORM_FLIPPED;
			break;
		case WL_OUTPUT_TRANSFORM_270:
			result_transform = WL_OUTPUT_TRANSFORM_FLIPPED_270;
			break;
		case WL_OUTPUT_TRANSFORM_FLIPPED:
			result_transform = WL_OUTPUT_TRANSFORM_180;
			break;
		case WL_OUTPUT_TRANSFORM_FLIPPED_90:
			result_transform = WL_OUTPUT_TRANSFORM_90;
			break;
		case WL_OUTPUT_TRANSFORM_FLIPPED_180:
			result_transform = WL_OUTPUT_TRANSFORM_NORMAL;
			break;
		case WL_OUTPUT_TRANSFORM_FLIPPED_270:
			result_transform = WL_OUTPUT_TRANSFORM_270;
			break;
		default:
			assert(false);
			break;

	}
	return result_transform;
}


static struct wl_interface *eglstream_controller_interface = NULL;

static void
attach_eglstream_consumer_attribs(struct wl_client *client,
			  struct wl_resource *resource,
			  struct wl_resource *wl_surface,
			  struct wl_resource *wl_eglstream,
			  struct wl_array *attribs)
{
	struct wlr_surface *surface = wlr_surface_from_resource(wl_surface);
	if (!surface->is_eglstream) {
		// egl surface should be y flipped opposed to drm surfaces
		surface->is_eglstream = true;
		surface->pending.committed |= WLR_SURFACE_STATE_TRANSFORM;
		surface->pending.transform =
			wlr_egl_normalize_output_transform(surface->current.transform); 
	}
	assert(surface);
	struct wlr_texture *texture =
		wlr_texture_from_wl_eglstream(surface->renderer, wl_eglstream);
	if (texture == NULL) {
		wlr_log(WLR_ERROR, "Failed to upload buffer");
		// Failing to attach stream here leads to a deadlock in 
		// nvidia driver. Abort.
		abort();
		return;
	}
	// stream is cached now
	wlr_texture_destroy(texture);
}

static void
attach_eglstream_consumer(struct wl_client *client,
			  struct wl_resource *resource,
			  struct wl_resource *wl_surface,
			  struct wl_resource *wl_eglstream) {
	attach_eglstream_consumer_attribs(client, resource,
			wl_surface, wl_eglstream, NULL);
}

static const struct wl_eglstream_controller_interface
eglstream_controller_implementation = {
	attach_eglstream_consumer,
	attach_eglstream_consumer_attribs,
};

static void
bind_eglstream_controller(struct wl_client *client,
			  void *data, uint32_t version, uint32_t id)
{
	struct wl_resource *resource;
	pid_t pid; uid_t uid; gid_t gid;
	wl_client_get_credentials(client, &pid, &uid, &gid);
wlr_log(WLR_INFO, "client: %u, %u, %u", pid, uid, gid);
	/*if (getpid() == pid) {*/
		/*wlr_log(WLR_INFO, "Disabling eglstream_controller for current process");*/
		/*return;*/
	/*}*/
	resource = wl_resource_create(client, eglstream_controller_interface,
				      version, id);

	if (resource == NULL) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(resource,
				       &eglstream_controller_implementation,
				       data,
				       NULL);
}

void init_eglstream_controller(struct wl_display *display)
{
	/*
	 * wl_eglstream_controller_interface is provided by
	 * libnvidia-egl-wayland.so.1
	 *
	 * Since it might not be available on the
	 * system, dynamically load it at runtime and resolve the needed
	 * symbols. If available, it should be found under any of the search
	 * directories of dlopen()
	 *
	 * Failure to initialize wl_eglstream_controller is non-fatal
	 */

	void *lib = dlopen("libnvidia-egl-wayland.so.1", RTLD_NOW | RTLD_LAZY);
	if (!lib)
		goto fail;

	eglstream_controller_interface =
		dlsym(lib, "wl_eglstream_controller_interface");

	if (!eglstream_controller_interface)
		goto fail;

	if (wl_global_create(display,
			     eglstream_controller_interface, 2,
			     NULL, bind_eglstream_controller))
		return; /* success */
fail:
	if (lib)
		dlclose(lib);
	wlr_log(WLR_ERROR,
			"Unable to initialize wl_eglstream_controller.");
}

