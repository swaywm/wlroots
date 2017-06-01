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

#include "types.h"
#include "backend.h"
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

bool wlr_drm_renderer_init(struct wlr_drm_renderer *renderer, int fd) {
	renderer->gbm = gbm_create_device(fd);
	if (!renderer->gbm) {
		wlr_log(L_ERROR, "Failed to create GBM device: %s", strerror(errno));
		return false;
	}

	if (!wlr_egl_init(&renderer->egl, EGL_PLATFORM_GBM_MESA, renderer->gbm)) {
		gbm_device_destroy(renderer->gbm);
		return false;
	}

	renderer->fd = fd;
	return true;
}

void wlr_drm_renderer_free(struct wlr_drm_renderer *renderer) {
	if (!renderer) {
		return;
	}
	wlr_egl_free(&renderer->egl);
	gbm_device_destroy(renderer->gbm);
}

static void free_fb(struct gbm_bo *bo, void *data) {
	uint32_t *id = data;

	if (id && *id) {
		drmModeRmFB(gbm_bo_get_fd(bo), *id);
	}

	free(id);
}

static uint32_t get_fb_for_bo(int fd, struct gbm_bo *bo) {
	uint32_t *id = gbm_bo_get_user_data(bo);

	if (id) {
		return *id;
	}

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

static void wlr_drm_output_begin(struct wlr_output_state *output) {
	struct wlr_drm_renderer *renderer = output->renderer;
	eglMakeCurrent(renderer->egl.display, output->egl,
			output->egl, renderer->egl.context);
}

static void wlr_drm_output_end(struct wlr_output_state *output) {
	struct wlr_drm_renderer *renderer = output->renderer;

	if (!eglSwapBuffers(renderer->egl.display, output->egl)) {
		return;
	}
	struct gbm_bo *bo = gbm_surface_lock_front_buffer(output->gbm);
	if (!bo) {
		return;
	}
	uint32_t fb_id = get_fb_for_bo(renderer->fd, bo);
	drmModePageFlip(renderer->fd, output->crtc, fb_id, DRM_MODE_PAGE_FLIP_EVENT, output);
	gbm_surface_release_buffer(output->gbm, bo);
	output->pageflip_pending = true;
}

void wlr_drm_output_start_renderer(struct wlr_output_state *output) {
	if (output->state != DRM_OUTPUT_CONNECTED) {
		return;
	}

	struct wlr_drm_renderer *renderer = output->renderer;
	struct wlr_output_mode *mode = output->wlr_output->current_mode;

	// Render black frame
	eglMakeCurrent(renderer->egl.display, output->egl, output->egl, renderer->egl.context);

	glViewport(0, 0, output->width, output->height);
	glClearColor(0.0, 0.0, 0.0, 1.0);
	glClear(GL_COLOR_BUFFER_BIT);

	eglSwapBuffers(renderer->egl.display, output->egl);

	struct gbm_bo *bo = gbm_surface_lock_front_buffer(output->gbm);
	uint32_t fb_id = get_fb_for_bo(renderer->fd, bo);

	drmModeSetCrtc(renderer->fd, output->crtc, fb_id, 0, 0,
			&output->connector, 1, &mode->state->mode);
	drmModePageFlip(renderer->fd, output->crtc, fb_id,
			DRM_MODE_PAGE_FLIP_EVENT, output);

	gbm_surface_release_buffer(output->gbm, bo);
}

static bool display_init_renderer(struct wlr_drm_renderer *renderer,
	struct wlr_output_state *output) {
	struct wlr_output_mode *mode = output->wlr_output->current_mode;
	output->renderer = renderer;
	output->gbm = gbm_surface_create(renderer->gbm, mode->width,
		mode->height, GBM_FORMAT_XRGB8888, GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
	if (!output->gbm) {
		wlr_log(L_ERROR, "Failed to create GBM surface for %s: %s", output->name,
			strerror(errno));
		return false;
	}

	output->egl = wlr_egl_create_surface(&renderer->egl, output->gbm);
	if (output->egl == EGL_NO_SURFACE) {
		wlr_log(L_ERROR, "Failed to create EGL surface for %s", output->name);
		return false;
	}

	wlr_drm_output_start_renderer(output);
	return true;
}

static int find_id(const void *item, const void *cmp_to) {
	const struct wlr_output_state *output = item;
	const uint32_t *id = cmp_to;

	if (output->connector < *id) {
		return -1;
	} else if (output->connector > *id) {
		return 1;
	} else {
		return 0;
	}
}

static bool wlr_drm_output_set_mode(struct wlr_output_state *output,
		struct wlr_output_mode *mode) {
	struct wlr_backend_state *state =
		wl_container_of(output->renderer, state, renderer);

	wlr_log(L_INFO, "Modesetting '%s' with '%ux%u@%u mHz'", output->name,
			mode->width, mode->height, mode->refresh);

	drmModeConnector *conn = drmModeGetConnector(state->fd, output->connector);
	if (!conn) {
		wlr_log(L_ERROR, "Failed to get DRM connector");
		goto error;
	}

	if (conn->connection != DRM_MODE_CONNECTED || conn->count_modes == 0) {
		wlr_log(L_ERROR, "%s is not connected", output->name);
		goto error;
	}

	drmModeRes *res = drmModeGetResources(state->fd);
	if (!res) {
		wlr_log(L_ERROR, "Failed to get DRM resources");
		goto error;
	}

	bool success = false;
	for (int i = 0; !success && i < conn->count_encoders; ++i) {
		drmModeEncoder *enc = drmModeGetEncoder(state->fd, conn->encoders[i]);
		if (!enc) {
			continue;
		}

		for (int j = 0; j < res->count_crtcs; ++j) {
			if ((enc->possible_crtcs & (1 << j)) == 0) {
				continue;
			}

			if ((state->taken_crtcs & (1 << j)) == 0) {
				state->taken_crtcs |= 1 << j;
				output->crtc = res->crtcs[j];
				success = true;
				break;
			}
		}
		drmModeFreeEncoder(enc);
	}

	drmModeFreeResources(res);

	if (!success) {
		wlr_log(L_ERROR, "Failed to find CRTC for %s", output->name);
		goto error;
	}

	output->state = DRM_OUTPUT_CONNECTED;
	output->width = mode->width;
	output->height = mode->height;
	output->wlr_output->current_mode = mode;

	if (!display_init_renderer(&state->renderer, output)) {
		wlr_log(L_ERROR, "Failed to initalise renderer for %s", output->name);
		goto error;
	}

	drmModeFreeConnector(conn);
	return true;

error:
	wlr_drm_output_cleanup(output, false);
	drmModeFreeConnector(conn);
	return false;
}

static void wlr_drm_output_enable(struct wlr_output_state *output, bool enable) {
	struct wlr_backend_state *state =
		wl_container_of(output->renderer, state, renderer);
	if (output->state != DRM_OUTPUT_CONNECTED) {
		return;
	}

	if (enable) {
		drmModeConnectorSetProperty(state->fd, output->connector, output->props.dpms,
			DRM_MODE_DPMS_ON);

		// Start rendering loop again by drawing a black frame
		wlr_drm_output_begin(output);
		glClearColor(0.0, 0.0, 0.0, 1.0);
		glClear(GL_COLOR_BUFFER_BIT);
		wlr_drm_output_end(output);
	} else {
		drmModeConnectorSetProperty(state->fd, output->connector, output->props.dpms,
			DRM_MODE_DPMS_STANDBY);
	}
}

static void wlr_drm_output_destroy(struct wlr_output_state *output) {
	wlr_drm_output_cleanup(output, true);
	wlr_drm_renderer_free(output->renderer);
	free(output);
}

static struct wlr_output_impl output_impl = {
	.set_mode = wlr_drm_output_set_mode,
	.enable = wlr_drm_output_enable,
	.destroy = wlr_drm_output_destroy,
};

static int32_t calculate_refresh_rate(drmModeModeInfo *mode) {
	int32_t refresh = (mode->clock * 1000000LL / mode->htotal +
		mode->vtotal / 2) / mode->vtotal;

	if (mode->flags & DRM_MODE_FLAG_INTERLACE)
		refresh *= 2;
	if (mode->flags & DRM_MODE_FLAG_DBLSCAN)
		refresh /= 2;
	if (mode->vscan > 1)
		refresh /= mode->vscan;

	return refresh;
}

static void scan_property_ids(int fd, drmModeConnector *conn,
		struct wlr_output_state *output) {
	for (int i = 0; i < conn->count_props; ++i) {
		drmModePropertyRes *prop = drmModeGetProperty(fd, conn->props[i]);
		if (!prop) {
			continue;
		}

		// I think this is guranteed to exist
		if (strcmp(prop->name, "DPMS") == 0) {
			output->props.dpms = prop->prop_id;

			/* There may be more properties we want to get,
			 * but since it's currently only this, we exit early
			 */

			drmModeFreeProperty(prop);
			break;
		}

		drmModeFreeProperty(prop);
	}
}

void wlr_drm_scan_connectors(struct wlr_backend_state *state) {
	wlr_log(L_INFO, "Scanning DRM connectors");

	drmModeRes *res = drmModeGetResources(state->fd);
	if (!res) {
		wlr_log(L_ERROR, "Failed to get DRM resources");
		return;
	}

	for (int i = 0; i < res->count_connectors; ++i) {
		uint32_t id = res->connectors[i];

		drmModeConnector *conn = drmModeGetConnector(state->fd, id);
		if (!conn) {
			wlr_log(L_ERROR, "Failed to get DRM connector");
			continue;
		}

		struct wlr_output_state *output;
		struct wlr_output *wlr_output;
		int index = list_seq_find(state->outputs, find_id, &id);

		if (index == -1) {
			output = calloc(1, sizeof(struct wlr_output_state));
			if (!state) {
				drmModeFreeConnector(conn);
				wlr_log(L_ERROR, "Allocation failed: %s", strerror(errno));
				return;
			}
			wlr_output = output->wlr_output = wlr_output_create(&output_impl, output);
			if (!wlr_output) {
				drmModeFreeConnector(conn);
				wlr_log(L_ERROR, "Allocation failed: %s", strerror(errno));
				return;
			}

			output->renderer = &state->renderer;
			output->state = DRM_OUTPUT_DISCONNECTED;
			output->connector = id;
			// TODO: Populate more wlr_output fields
			// TODO: Move this to wlr_output->name
			snprintf(output->name, sizeof(output->name), "%s-%"PRIu32,
				 conn_name[conn->connector_type],
				 conn->connector_type_id);

			drmModeEncoder *curr_enc = drmModeGetEncoder(state->fd, conn->encoder_id);
			if (curr_enc) {
				output->old_crtc = drmModeGetCrtc(state->fd, curr_enc->crtc_id);
				free(curr_enc);
			}

			scan_property_ids(state->fd, conn, output);

			list_add(state->outputs, output);
			wlr_log(L_INFO, "Found display '%s'", output->name);
		} else {
			output = state->outputs->items[index];
			wlr_output = output->wlr_output;
		}

		// TODO: move state into wlr_output
		if (output->state == DRM_OUTPUT_DISCONNECTED &&
			conn->connection == DRM_MODE_CONNECTED) {

			wlr_log(L_INFO, "'%s' connected", output->name);
			wlr_log(L_INFO, "Detected modes:");

			for (int i = 0; i < conn->count_modes; ++i) {
				struct wlr_output_mode_state *_state = calloc(1,
						sizeof(struct wlr_output_mode_state));
				_state->mode = conn->modes[i];
				struct wlr_output_mode *mode = calloc(1,
						sizeof(struct wlr_output_mode));
				mode->width = _state->mode.hdisplay;
				mode->height = _state->mode.vdisplay;
				mode->refresh = calculate_refresh_rate(&_state->mode);
				mode->state = _state;

				wlr_log(L_INFO, "  %"PRId32"@%"PRId32"@%"PRId32,
					mode->width, mode->height, mode->refresh);

				list_add(wlr_output->modes, mode);
			}

			output->state = DRM_OUTPUT_NEEDS_MODESET;
			wlr_log(L_INFO, "Sending modesetting signal for '%s'", output->name);
			wl_signal_emit(&state->backend->events.output_add, wlr_output);
		} else if (output->state == DRM_OUTPUT_CONNECTED &&
			conn->connection != DRM_MODE_CONNECTED) {

			wlr_log(L_INFO, "'%s' disconnected", output->name);
			wlr_drm_output_cleanup(output, false);
		}

		drmModeFreeConnector(conn);
	}

	drmModeFreeResources(res);
}

static void page_flip_handler(int fd, unsigned seq,
		unsigned tv_sec, unsigned tv_usec, void *user) {
	struct wlr_output_state *output = user;
	struct wlr_backend_state *state =
		wl_container_of(output->renderer, state, renderer);

	output->pageflip_pending = false;
	if (output->state == DRM_OUTPUT_CONNECTED) {
		wlr_drm_output_begin(output);
		wl_signal_emit(&output->wlr_output->events.frame, output->wlr_output);
		wlr_drm_output_end(output);
	}
}

int wlr_drm_event(int fd, uint32_t mask, void *data) {
	drmEventContext event = {
		.version = DRM_EVENT_CONTEXT_VERSION,
		.page_flip_handler = page_flip_handler,
	};

	drmHandleEvent(fd, &event);
	return 1;
}

static void restore_output(struct wlr_output_state *output, int fd) {
	// Wait for any pending pageflips to finish
	while (output->pageflip_pending) {
		wlr_drm_event(fd, 0, NULL);
	}

	drmModeCrtc *crtc = output->old_crtc;
	if (!crtc) {
		return;
	}

	drmModeSetCrtc(fd, crtc->crtc_id, crtc->buffer_id, crtc->x, crtc->y,
		&output->connector, 1, &crtc->mode);
	drmModeFreeCrtc(crtc);
}

void wlr_drm_output_cleanup(struct wlr_output_state *output, bool restore) {
	if (!output) {
		return;
	}

	struct wlr_drm_renderer *renderer = output->renderer;
	struct wlr_backend_state *state = wl_container_of(renderer, state, renderer);

	switch (output->state) {
	case DRM_OUTPUT_CONNECTED:
		output->state = DRM_OUTPUT_DISCONNECTED;
		if (restore) {
			restore_output(output, renderer->fd);
			restore = false;
		}
		eglDestroySurface(renderer->egl.display, output->egl);
		gbm_surface_destroy(output->gbm);
		output->egl = EGL_NO_SURFACE;
		output->gbm = NULL;
		/* Fallthrough */
	case DRM_OUTPUT_NEEDS_MODESET:
		output->state = DRM_OUTPUT_DISCONNECTED;
		if (restore) {
			restore_output(output, renderer->fd);
		}
		wlr_log(L_INFO, "Emmiting destruction signal for '%s'", output->name);
		wl_signal_emit(&state->backend->events.output_remove, output->wlr_output);
		break;
	case DRM_OUTPUT_DISCONNECTED:
		break;
	}
}
