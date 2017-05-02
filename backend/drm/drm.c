#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <errno.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_mode.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <gbm.h>
#include <GLES3/gl3.h>
#include <wayland-server.h>

#include "backend/drm/backend.h"
#include "backend/drm/drm.h"
#include "common/log.h"

static const char *conn_name[] = {
	[DRM_MODE_CONNECTOR_Unknown]     = "Unknown",
	[DRM_MODE_CONNECTOR_VGA]         = "VGA",
	[DRM_MODE_CONNECTOR_DVII]        = "DVI-I",
	[DRM_MODE_CONNECTOR_DVID]        = "DVI-D",
	[DRM_MODE_CONNECTOR_DVIA]        = "DVI-A",
	[DRM_MODE_CONNECTOR_Composite]   = "Composite",
	[DRM_MODE_CONNECTOR_SVIDEO]      = "SVIDEO",
	[DRM_MODE_CONNECTOR_LVDS]        = "LVDS",
	[DRM_MODE_CONNECTOR_Component]   = "Component",
	[DRM_MODE_CONNECTOR_9PinDIN]     = "DIN",
	[DRM_MODE_CONNECTOR_DisplayPort] = "DP",
	[DRM_MODE_CONNECTOR_HDMIA]       = "HDMI-A",
	[DRM_MODE_CONNECTOR_HDMIB]       = "HDMI-B",
	[DRM_MODE_CONNECTOR_TV]          = "TV",
	[DRM_MODE_CONNECTOR_eDP]         = "eDP",
	[DRM_MODE_CONNECTOR_VIRTUAL]     = "Virtual",
	[DRM_MODE_CONNECTOR_DSI]         = "DSI",
};

static const char *egl_error(void)
{
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

// EGL extensions
PFNEGLGETPLATFORMDISPLAYEXTPROC get_platform_display;
PFNEGLCREATEPLATFORMWINDOWSURFACEEXTPROC create_platform_window_surface;

static bool egl_exts()
{
	get_platform_display = (PFNEGLGETPLATFORMDISPLAYEXTPROC)
		eglGetProcAddress("eglGetPlatformDisplayEXT");

	if (!get_platform_display) {
		wlr_log(L_ERROR, "Failed to load EGL extension 'eglGetPlatformDisplayEXT'");
		return false;
	}

	create_platform_window_surface = (PFNEGLCREATEPLATFORMWINDOWSURFACEEXTPROC)
		eglGetProcAddress("eglCreatePlatformWindowSurfaceEXT");

	if (!get_platform_display) {
		wlr_log(L_ERROR, "Failed to load EGL extension 'eglCreatePlatformWindowSurfaceEXT'");
		return false;
	}

	return true;
}

static bool egl_get_config(EGLDisplay disp, EGLConfig *out)
{
	EGLint count = 0, matched = 0, ret;

	ret = eglGetConfigs(disp, NULL, 0, &count);
	if (ret == EGL_FALSE || count == 0) {
		return false;
	}

	EGLConfig configs[count];

	ret = eglChooseConfig(disp, NULL, configs, count, &matched);
	if (ret == EGL_FALSE) {
		return false;
	}

	for (int i = 0; i < matched; ++i) {
		EGLint gbm_format;

		if (!eglGetConfigAttrib(disp,
					configs[i],
					EGL_NATIVE_VISUAL_ID,
					&gbm_format))
			continue;

		if (gbm_format == GBM_FORMAT_XRGB8888) {
			*out = configs[i];
			return true;
		}
	}

	return false;
}

static void page_flip_handler(int fd,
		unsigned seq,
		unsigned tv_sec,
		unsigned tv_usec,
		void *user)
{
	struct wlr_drm_display *disp = user;
	struct wlr_drm_backend *backend = disp->renderer->backend;

	disp->pageflip_pending = true;
	if (!disp->cleanup)
		wl_signal_emit(&backend->signals.display_render, disp);
}


static int drm_event(int fd, uint32_t mask, void *data)
{
	drmEventContext event = {
		.version = DRM_EVENT_CONTEXT_VERSION,
		.page_flip_handler = page_flip_handler,
	};

	drmHandleEvent(fd, &event);

	return 1;
}

bool wlr_drm_renderer_init(struct wlr_drm_renderer *renderer,
		struct wlr_drm_backend *backend, int fd)
{
	if (!egl_exts())
		return false;

	renderer->gbm = gbm_create_device(fd);
	if (!renderer->gbm) {
		wlr_log(L_ERROR, "Failed to create GBM device: %s", strerror(errno));
		return false;
	}

	if (eglBindAPI(EGL_OPENGL_ES_API) == EGL_FALSE) {
		wlr_log(L_ERROR, "Failed to bind to the OpenGL ES API: %s", egl_error());
		goto error_gbm;
	}

	renderer->egl.disp = get_platform_display(EGL_PLATFORM_GBM_MESA, renderer->gbm, NULL);
	if (renderer->egl.disp == EGL_NO_DISPLAY) {
		wlr_log(L_ERROR, "Failed to create EGL display: %s", egl_error());
		goto error_gbm;
	}

	if (eglInitialize(renderer->egl.disp, NULL, NULL) == EGL_FALSE) {
		wlr_log(L_ERROR, "Failed to initialize EGL: %s", egl_error());
		goto error_egl;
	}

	if (!egl_get_config(renderer->egl.disp, &renderer->egl.conf)) {
		wlr_log(L_ERROR, "Failed to get EGL config");
		goto error_egl;
	}

	static const EGLint attribs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};

	renderer->egl.context = eglCreateContext(renderer->egl.disp,
			renderer->egl.conf, EGL_NO_CONTEXT, attribs);

	if (renderer->egl.context == EGL_NO_CONTEXT) {
		wlr_log(L_ERROR, "Failed to create EGL context: %s", egl_error());
		goto error_egl;
	}

	backend->event_src.drm = wl_event_loop_add_fd(backend->event_loop,
			backend->fd, WL_EVENT_READABLE, drm_event, NULL);
	if (!backend->event_src.drm) {
		wlr_log(L_ERROR, "Failed to create DRM event source");
		goto error_egl;
	}

	renderer->fd = fd;
	renderer->backend = backend;

	return true;

error_egl:
	eglTerminate(renderer->egl.disp);
	eglReleaseThread();
	eglMakeCurrent(EGL_NO_DISPLAY, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);

error_gbm:
	gbm_device_destroy(renderer->gbm);

	return false;
}

void wlr_drm_renderer_free(struct wlr_drm_renderer *renderer)
{
	if (!renderer)
		return;

	eglDestroyContext(renderer->egl.disp, renderer->egl.context);
	eglTerminate(renderer->egl.disp);
	eglReleaseThread();
	eglMakeCurrent(EGL_NO_DISPLAY, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);

	gbm_device_destroy(renderer->gbm);
}

static int find_id(const void *item, const void *cmp_to)
{
	const struct wlr_drm_display *disp = item;
	const uint32_t *id = cmp_to;

	if (disp->connector < *id)
		return -1;
	else if (disp->connector > *id)
		return 1;
	else
		return 0;
}

void wlr_drm_scan_connectors(struct wlr_drm_backend *backend)
{
	drmModeRes *res = drmModeGetResources(backend->fd);
	if (!res) {
		wlr_log(L_ERROR, "Failed to get DRM resources");
		return;
	}

	for (int i = 0; i < res->count_connectors; ++i) {
		uint32_t id = res->connectors[i];

		drmModeConnector *conn = drmModeGetConnector(backend->fd, id);
		if (!conn) {
			wlr_log(L_ERROR, "Failed to get DRM connector");
			continue;
		}

		struct wlr_drm_display *disp;
		int index = list_seq_find(backend->displays, find_id, &id);

		if (index == -1) {
			disp = calloc(1, sizeof *disp);
			if (!disp) {
				wlr_log(L_ERROR, "Allocation failed: %s", strerror(errno));
				drmModeFreeConnector(conn);
				continue;
			}

			disp->renderer = &backend->renderer;
			disp->state = DRM_DISP_DISCONNECTED;
			disp->connector = res->connectors[i];
			snprintf(disp->name, sizeof disp->name, "%s-%"PRIu32,
				 conn_name[conn->connector_type],
				 conn->connector_type_id);

			list_add(backend->displays, disp);
			wlr_log(L_INFO, "Found display '%s'", disp->name);
		} else {
			disp = backend->displays->items[index];
		}

		if (disp->state == DRM_DISP_DISCONNECTED &&
		    conn->connection == DRM_MODE_CONNECTED) {
			disp->state = DRM_DISP_NEEDS_MODESET;
			wlr_log(L_INFO, "Sending modesetting signal for '%s'", disp->name);
			wl_signal_emit(&backend->signals.display_add, disp);

		} else if (disp->state == DRM_DISP_CONNECTED &&
		    conn->connection != DRM_MODE_CONNECTED) {
			disp->state = DRM_DISP_DISCONNECTED;
			wlr_drm_display_free(disp, false);
			wlr_log(L_INFO, "Sending destruction signal for '%s'", disp->name);
			wl_signal_emit(&backend->signals.display_rem, disp);
		}

		drmModeFreeConnector(conn);
	}

	drmModeFreeResources(res);
}

static void free_fb(struct gbm_bo *bo, void *data)
{
	uint32_t *id = data;

	if (id && *id)
		drmModeRmFB(gbm_bo_get_fd(bo), *id);

	free(id);
}

static uint32_t get_fb_for_bo(int fd, struct gbm_bo *bo)
{
	uint32_t *id = gbm_bo_get_user_data(bo);

	if (id)
		return *id;

	id = calloc(1, sizeof *id);
	if (!id) {
		wlr_log(L_ERROR, "Allocation failed: %s", strerror(errno));
		return 0;
	}

	drmModeAddFB(fd, gbm_bo_get_width(bo), gbm_bo_get_height(bo), 24, 32,
		     gbm_bo_get_stride(bo), gbm_bo_get_handle(bo).u32, id);

	gbm_bo_set_user_data(bo, id, free_fb);

	return *id;
}

static bool display_init_renderer(struct wlr_drm_renderer *renderer,
		struct wlr_drm_display *disp)
{
	disp->renderer = renderer;

	disp->gbm = gbm_surface_create(renderer->gbm,
				       disp->width, disp->height,
				       GBM_FORMAT_XRGB8888,
				       GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
	if (!disp->gbm) {
		wlr_log(L_ERROR, "Failed to create GBM surface for %s: %s", disp->name,
			strerror(errno));
		return false;
	}

	disp->egl = create_platform_window_surface(renderer->egl.disp, renderer->egl.conf,
						   disp->gbm, NULL);
	if (disp->egl == EGL_NO_SURFACE) {
		wlr_log(L_ERROR, "Failed to create EGL surface for %s: %s", disp->name,
			egl_error());
		return false;
	}

	// Render black frame

	eglMakeCurrent(renderer->egl.disp, disp->egl, disp->egl, renderer->egl.context);

	glViewport(0, 0, disp->width, disp->height);
	glClearColor(0.0, 0.0, 0.0, 1.0);
	glClear(GL_COLOR_BUFFER_BIT);

	eglSwapBuffers(renderer->egl.disp, disp->egl);

	struct gbm_bo *bo = gbm_surface_lock_front_buffer(disp->gbm);
	uint32_t fb_id = get_fb_for_bo(renderer->fd, bo);

	drmModeSetCrtc(renderer->fd, disp->crtc, fb_id, 0, 0,
		       &disp->connector, 1, disp->active_mode);
	drmModePageFlip(renderer->fd, disp->crtc, fb_id, DRM_MODE_PAGE_FLIP_EVENT, disp);

	gbm_surface_release_buffer(disp->gbm, bo);

	return true;
}

static drmModeModeInfo *select_mode(size_t num_modes,
		drmModeModeInfo modes[static num_modes],
		drmModeCrtc *old_crtc,
		const char *str)
{
	if (strcmp(str, "preferred") == 0)
		return &modes[0];

	if (strcmp(str, "current") == 0) {
		if (!old_crtc) {
			wlr_log(L_ERROR, "Display does not have currently configured mode");
			return NULL;
		}

		for (size_t i = 0; i < num_modes; ++i) {
			if (memcmp(&modes[i], &old_crtc->mode, sizeof modes[0]) == 0)
				return &modes[i];
		}

		// We should never get here
		assert(0);
	}

	unsigned width = 0;
	unsigned height = 0;
	unsigned rate = 0;
	int ret;

	if ((ret = sscanf(str, "%ux%u@%u", &width, &height, &rate)) != 2 && ret != 3) {
		wlr_log(L_ERROR, "Invalid modesetting string '%s'", str);
		return NULL;
	}

	for (size_t i = 0; i < num_modes; ++i) {
		if (modes[i].hdisplay == width &&
		    modes[i].vdisplay == height &&
		    (!rate || modes[i].vrefresh == rate))
			return &modes[i];
	}

	wlr_log(L_ERROR, "Unable to find mode %ux%u@%u", width, height, rate);
	return NULL;
}

bool wlr_drm_display_modeset(struct wlr_drm_display *disp, const char *str)
{
	struct wlr_drm_backend *backend = disp->renderer->backend;
	wlr_log(L_INFO, "Modesetting %s with '%s'", disp->name, str);

	drmModeConnector *conn = drmModeGetConnector(backend->fd, disp->connector);
	if (!conn) {
		wlr_log(L_ERROR, "Failed to get DRM connector");
		goto error;
	}

	if (conn->connection != DRM_MODE_CONNECTED || conn->count_modes == 0) {
		wlr_log(L_ERROR, "%s is not connected", disp->name);
		goto error;
	}

	disp->num_modes = conn->count_modes;
	disp->modes = malloc(sizeof *disp->modes * disp->num_modes);
	if (!disp->modes) {
		wlr_log(L_ERROR, "Allocation failed: %s", strerror(errno));
		goto error;
	}
	memcpy(disp->modes, conn->modes, sizeof *disp->modes * disp->num_modes);

	wlr_log(L_INFO, "Detected modes:");
	for (size_t i = 0; i < disp->num_modes; ++i)
		wlr_log(L_INFO, "  %"PRIu16"@%"PRIu16"@%"PRIu32,
			disp->modes[i].hdisplay, disp->modes[i].vdisplay,
			disp->modes[i].vrefresh);

	drmModeEncoder *curr_enc = drmModeGetEncoder(backend->fd, conn->encoder_id);
	if (curr_enc) {
		disp->old_crtc = drmModeGetCrtc(backend->fd, curr_enc->crtc_id);
		free(curr_enc);
	}

	disp->active_mode = select_mode(disp->num_modes, disp->modes, disp->old_crtc, str);
	if (!disp->active_mode) {
		wlr_log(L_ERROR, "Failed to configure %s", disp->name);
		goto error;
	}

	drmModeRes *res = drmModeGetResources(backend->fd);
	if (!res) {
		wlr_log(L_ERROR, "Failed to get DRM resources");
		goto error;
	}

	bool success = false;
	for (int i = 0; !success && i < conn->count_encoders; ++i) {
		drmModeEncoder *enc = drmModeGetEncoder(backend->fd, conn->encoders[i]);
		if (!enc)
			continue;

		for (int j = 0; j < res->count_crtcs; ++j) {
			if ((enc->possible_crtcs & (1 << j)) == 0)
				continue;

			if ((backend->taken_crtcs & (1 << j)) == 0) {
				backend->taken_crtcs |= 1 << j;
				disp->crtc = res->crtcs[j];

				success = true;
				break;
			}
		}

		drmModeFreeEncoder(enc);
	}

	drmModeFreeResources(res);

	if (!success) {
		wlr_log(L_ERROR, "Failed to find CRTC for %s", disp->name);
		goto error;
	}

	disp->state = DRM_DISP_CONNECTED;

	disp->width = disp->active_mode->hdisplay;
	disp->height = disp->active_mode->vdisplay;

	if (!display_init_renderer(&backend->renderer, disp)) {
		wlr_log(L_ERROR, "Failed to initalise renderer for %s", disp->name);
		goto error;
	}

	drmModeFreeConnector(conn);

	wlr_log(L_INFO, "Configuring %s with mode %"PRIu16"x%"PRIu16"@%"PRIu32"",
		disp->name, disp->active_mode->hdisplay, disp->active_mode->vdisplay,
		disp->active_mode->vrefresh);

	return true;

error:
	disp->state = DRM_DISP_DISCONNECTED;
	drmModeFreeConnector(conn);
	free(disp->modes);

	wl_signal_emit(&backend->signals.display_rem, disp);

	return false;
}

void wlr_drm_display_free(struct wlr_drm_display *disp, bool restore)
{
	if (!disp || disp->state != DRM_DISP_CONNECTED)
		return;

	struct wlr_drm_renderer *renderer = disp->renderer;

	eglDestroySurface(renderer->egl.disp, disp->egl);
	gbm_surface_destroy(disp->gbm);

	free(disp->modes);
	disp->state = DRM_DISP_DISCONNECTED;

	if (!restore)
		return;

	drmModeCrtc *crtc = disp->old_crtc;
	if (crtc) {
		// Wait for exising page flips to finish

		drmEventContext event = {
			.version = DRM_EVENT_CONTEXT_VERSION,
			.page_flip_handler = page_flip_handler,
		};

		disp->cleanup = true;
		while (disp->pageflip_pending)
			drmHandleEvent(renderer->fd, &event);

		drmModeSetCrtc(renderer->fd, crtc->crtc_id, crtc->buffer_id,
			       crtc->x, crtc->y, &disp->connector,
			       1, &crtc->mode);
		drmModeFreeCrtc(crtc);
	}
}

void wlr_drm_display_begin(struct wlr_drm_display *disp)
{
	struct wlr_drm_renderer *renderer = disp->renderer;
	eglMakeCurrent(renderer->egl.disp, disp->egl, disp->egl, renderer->egl.context);
}

void wlr_drm_display_end(struct wlr_drm_display *disp)
{
	struct wlr_drm_renderer *renderer = disp->renderer;
	eglSwapBuffers(renderer->egl.disp, disp->egl);

	struct gbm_bo *bo = gbm_surface_lock_front_buffer(disp->gbm);
	uint32_t fb_id = get_fb_for_bo(renderer->fd, bo);

	drmModePageFlip(renderer->fd, disp->crtc, fb_id, DRM_MODE_PAGE_FLIP_EVENT, disp);

	gbm_surface_release_buffer(disp->gbm, bo);

	disp->pageflip_pending = false;
}
