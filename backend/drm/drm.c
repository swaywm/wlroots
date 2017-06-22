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
#include <wlr/backend/interface.h>
#include <wlr/interfaces/wlr_output.h>
#include <wlr/util/log.h>
#include "backend/drm.h"

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
	output->pageflip_pending = true;

	output->bo[1] = output->bo[0];
	output->bo[0] = bo;
}

void wlr_drm_output_pause_renderer(struct wlr_output_state *output) {
	if (output->state != DRM_OUTPUT_CONNECTED) {
		return;
	}

	if (output->bo[1]) {
		gbm_surface_release_buffer(output->gbm, output->bo[1]);
		output->bo[1] = NULL;
	}
	if (output->bo[0]) {
		gbm_surface_release_buffer(output->gbm, output->bo[0]);
		output->bo[0] = NULL;
	}
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

	output->bo[1] = NULL;
	output->bo[0] = bo;
}

static bool display_init_renderer(struct wlr_drm_renderer *renderer,
	struct wlr_output_state *output) {
	struct wlr_output_mode *mode = output->wlr_output->current_mode;
	output->renderer = renderer;
	output->gbm = gbm_surface_create(renderer->gbm, mode->width,
		mode->height, GBM_FORMAT_XRGB8888, GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
	if (!output->gbm) {
		wlr_log(L_ERROR, "Failed to create GBM surface for %s: %s", output->wlr_output->name,
			strerror(errno));
		return false;
	}

	output->egl = wlr_egl_create_surface(&renderer->egl, output->gbm);
	if (output->egl == EGL_NO_SURFACE) {
		wlr_log(L_ERROR, "Failed to create EGL surface for %s", output->wlr_output->name);
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

static bool wlr_drm_output_set_mode(struct wlr_output_state *output,
		struct wlr_output_mode *mode) {
	struct wlr_backend_state *state =
		wl_container_of(output->renderer, state, renderer);

	wlr_log(L_INFO, "Modesetting '%s' with '%ux%u@%u mHz'", output->wlr_output->name,
			mode->width, mode->height, mode->refresh);

	drmModeConnector *conn = drmModeGetConnector(state->fd, output->connector);
	if (!conn) {
		wlr_log(L_ERROR, "Failed to get DRM connector");
		goto error;
	}

	if (conn->connection != DRM_MODE_CONNECTED || conn->count_modes == 0) {
		wlr_log(L_ERROR, "%s is not connected", output->wlr_output->name);
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
		wlr_log(L_ERROR, "Failed to find CRTC for %s", output->wlr_output->name);
		goto error;
	}

	output->state = DRM_OUTPUT_CONNECTED;
	output->width = output->wlr_output->width = mode->width;
	output->height = output->wlr_output->height = mode->height;
	output->wlr_output->current_mode = mode;
	wl_signal_emit(&output->wlr_output->events.resolution, output->wlr_output);

	if (!display_init_renderer(&state->renderer, output)) {
		wlr_log(L_ERROR, "Failed to initalise renderer for %s", output->wlr_output->name);
		goto error;
	}

	drmModeFreeConnector(conn);
	return true;

error:
	wlr_drm_output_cleanup(output, false);
	drmModeFreeConnector(conn);
	return false;
}

static void wlr_drm_output_transform(struct wlr_output_state *output,
		enum wl_output_transform transform) {
	output->wlr_output->transform = transform;
}

static void wlr_drm_cursor_bo_update(struct wlr_output_state *output,
		uint32_t width, uint32_t height) {
	if (output->cursor_width == width && output->cursor_height == height) {
		return;
	}
	wlr_log(L_DEBUG, "Allocating new cursor bos");
	struct wlr_backend_state *state =
		wl_container_of(output->renderer, state, renderer);
	for (size_t i = 0; i < 2; ++i) {
		output->cursor_bo[i] = gbm_bo_create(state->renderer.gbm,
				width, height, GBM_FORMAT_ARGB8888,
				GBM_BO_USE_CURSOR | GBM_BO_USE_WRITE);
		if (!output->cursor_bo[i]) {
			wlr_log(L_ERROR, "Failed to create cursor bo");
			return;
		}
		if (!get_fb_for_bo(state->fd, output->cursor_bo[i])) {
			wlr_log(L_ERROR, "Failed to create cursor fb");
			return;
		}
	}
}

static bool wlr_drm_output_set_cursor(struct wlr_output_state *output,
		const uint8_t *buf, int32_t stride, uint32_t width, uint32_t height) {
	struct wlr_backend_state *state =
		wl_container_of(output->renderer, state, renderer);
	if (!buf) {
		drmModeSetCursor(state->fd, output->crtc, 0, 0, 0);
		return true;
	}
	wlr_drm_cursor_bo_update(output, width, height);
	struct gbm_bo *bo;
	output->current_cursor ^= 1;
	bo = output->cursor_bo[output->current_cursor];
	uint32_t _buf[width * height];
	memset(_buf, 0, sizeof(_buf));
	for (size_t i = 0; i < height; ++i) {
		memcpy(_buf + i * width,
				buf + i * stride,
				width * 4);
	}
	if (gbm_bo_write(bo, _buf, sizeof(_buf)) < 0) {
		wlr_log(L_ERROR, "Failed to write cursor to bo");
		return false;
	}
	return !drmModeSetCursor(state->fd, output->crtc,
			gbm_bo_get_handle(bo).s32, width, height);
}

static bool wlr_drm_output_move_cursor(struct wlr_output_state *output,
		int x, int y) {
	struct wlr_backend_state *state =
		wl_container_of(output->renderer, state, renderer);
	return !drmModeMoveCursor(state->fd, output->crtc, x, y);
}

static void wlr_drm_output_destroy(struct wlr_output_state *output) {
	wlr_drm_output_cleanup(output, true);
	free(output);
}

static struct wlr_output_impl output_impl = {
	.enable = wlr_drm_output_enable,
	.set_mode = wlr_drm_output_set_mode,
	.transform = wlr_drm_output_transform,
	.set_cursor = wlr_drm_output_set_cursor,
	.move_cursor = wlr_drm_output_move_cursor,
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

// Constructed from http://edid.tv/manufacturer
const char *get_manufacturer(uint16_t id) {
#define ID(a, b, c) ((a & 0x1f) << 10) | ((b & 0x1f) << 5) | (c & 0x1f)
	switch (id) {
	case ID('A', 'A', 'A'): return "Avolites Ltd";
	case ID('A', 'C', 'I'): return "Ancor Communications Inc";
	case ID('A', 'C', 'R'): return "Acer Technologies";
	case ID('A', 'P', 'P'): return "Apple Computer Inc";
	case ID('B', 'N', 'O'): return "Bang & Olufsen";
	case ID('C', 'M', 'N'): return "Chimei Innolux Corporation";
	case ID('C', 'M', 'O'): return "Chi Mei Optoelectronics corp.";
	case ID('C', 'R', 'O'): return "Extraordinary Technologies PTY Limited";
	case ID('D', 'E', 'L'): return "Dell Inc.";
	case ID('D', 'O', 'N'): return "DENON, Ltd.";
	case ID('E', 'N', 'C'): return "Eizo Nanao Corporation";
	case ID('E', 'P', 'H'): return "Epiphan Systems Inc.";
	case ID('F', 'U', 'S'): return "Fujitsu Siemens Computers GmbH";
	case ID('G', 'S', 'M'): return "Goldstar Company Ltd";
	case ID('H', 'I', 'Q'): return "Kaohsiung Opto Electronics Americas, Inc.";
	case ID('H', 'S', 'D'): return "HannStar Display Corp";
	case ID('H', 'W', 'P'): return "Hewlett Packard";
	case ID('I', 'N', 'T'): return "Interphase Corporation";
	case ID('I', 'V', 'M'): return "Iiyama North America";
	case ID('L', 'E', 'N'): return "Lenovo Group Limited";
	case ID('M', 'A', 'X'): return "Rogen Tech Distribution Inc";
	case ID('M', 'E', 'G'): return "Abeam Tech Ltd";
	case ID('M', 'E', 'I'): return "Panasonic Industry Company";
	case ID('M', 'T', 'C'): return "Mars-Tech Corporation";
	case ID('M', 'T', 'X'): return "Matrox";
	case ID('N', 'E', 'C'): return "NEC Corporation";
	case ID('O', 'N', 'K'): return "ONKYO Corporation";
	case ID('O', 'R', 'N'): return "ORION ELECTRIC CO., LTD.";
	case ID('O', 'T', 'M'): return "Optoma Corporation";
	case ID('O', 'V', 'R'): return "Oculus VR, Inc.";
	case ID('P', 'H', 'L'): return "Philips Consumer Electronics Company";
	case ID('P', 'I', 'O'): return "Pioneer Electronic Corporation";
	case ID('P', 'N', 'R'): return "Planar Systems, Inc.";
	case ID('Q', 'D', 'S'): return "Quanta Display Inc.";
	case ID('S', 'A', 'M'): return "Samsung Electric Company";
	case ID('S', 'E', 'C'): return "Seiko Epson Corporation";
	case ID('S', 'H', 'P'): return "Sharp Corporation";
	case ID('S', 'I', 'I'): return "Silicon Image, Inc.";
	case ID('S', 'N', 'Y'): return "Sony";
	case ID('T', 'O', 'P'): return "Orion Communications Co., Ltd.";
	case ID('T', 'S', 'B'): return "Toshiba America Info Systems Inc";
	case ID('T', 'S', 'T'): return "Transtream Inc";
	case ID('U', 'N', 'K'): return "Unknown";
	case ID('V', 'I', 'Z'): return "VIZIO, Inc";
	case ID('V', 'S', 'C'): return "ViewSonic Corporation";
	case ID('Y', 'M', 'H'): return "Yamaha Corporation";
	default: return "Unknown";
	}
#undef ID
}

/* See https://en.wikipedia.org/wiki/Extended_Display_Identification_Data for layout of EDID data.
 * We don't parse the EDID properly. We just expect to receive valid data.
 */
static void parse_edid(struct wlr_output *restrict output, const uint8_t *restrict data) {
	uint32_t id = (data[8] << 8) | data[9];
	snprintf(output->make, sizeof(output->make), "%s", get_manufacturer(id));

	output->phys_width = ((data[68] & 0xf0) << 4) | data[66];
	output->phys_height = ((data[68] & 0x0f) << 8) | data[67];

	for (size_t i = 72; i <= 108; i += 18) {
		uint16_t flag = (data[i] << 8) | data[i + 1];
		if (flag == 0 && data[i + 3] == 0xFC) {
			sprintf(output->model, "%.13s", &data[i + 5]);

			// Monitor names are terminated by newline if they're too short
			char *nl = strchr(output->model, '\n');
			if (nl) {
				*nl = '\0';
			}

			break;
		}
	}
}

static void scan_property_ids(int fd, drmModeConnector *conn,
		struct wlr_output_state *output) {
	for (int i = 0; i < conn->count_props; ++i) {
		drmModePropertyRes *prop = drmModeGetProperty(fd, conn->props[i]);
		if (!prop) {
			continue;
		}

		if (strcmp(prop->name, "DPMS") == 0) {
			output->props.dpms = prop->prop_id;
		} else if (strcmp(prop->name, "EDID") == 0) {
			drmModePropertyBlobRes *edid = drmModeGetPropertyBlob(fd, conn->prop_values[i]);
			if (!edid) {
				drmModeFreeProperty(prop);
				continue;
			}

			parse_edid(output->wlr_output, edid->data);

			drmModeFreePropertyBlob(edid);
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
			snprintf(wlr_output->name, sizeof(wlr_output->name), "%s-%"PRIu32,
				 conn_name[conn->connector_type],
				 conn->connector_type_id);
			wlr_output->phys_width = conn->mmWidth;
			wlr_output->phys_height = conn->mmHeight;
			switch (conn->subpixel) {
			case DRM_MODE_SUBPIXEL_UNKNOWN:
				wlr_output->subpixel = WL_OUTPUT_SUBPIXEL_UNKNOWN;
				break;
			case DRM_MODE_SUBPIXEL_HORIZONTAL_RGB:
				wlr_output->subpixel = WL_OUTPUT_SUBPIXEL_HORIZONTAL_RGB;
				break;
			case DRM_MODE_SUBPIXEL_HORIZONTAL_BGR:
				wlr_output->subpixel = WL_OUTPUT_SUBPIXEL_HORIZONTAL_BGR;
				break;
			case DRM_MODE_SUBPIXEL_VERTICAL_RGB:
				wlr_output->subpixel = WL_OUTPUT_SUBPIXEL_VERTICAL_RGB;
				break;
			case DRM_MODE_SUBPIXEL_VERTICAL_BGR:
				wlr_output->subpixel = WL_OUTPUT_SUBPIXEL_VERTICAL_BGR;
				break;
			case DRM_MODE_SUBPIXEL_NONE:
			default:
				wlr_output->subpixel = WL_OUTPUT_SUBPIXEL_NONE;
				break;
			}

			drmModeEncoder *curr_enc = drmModeGetEncoder(state->fd, conn->encoder_id);
			if (curr_enc) {
				output->old_crtc = drmModeGetCrtc(state->fd, curr_enc->crtc_id);
				free(curr_enc);
			}

			scan_property_ids(state->fd, conn, output);

			wlr_output_create_global(wlr_output, state->display);
			list_add(state->outputs, output);
			wlr_log(L_INFO, "Found display '%s'", wlr_output->name);
		} else {
			output = state->outputs->items[index];
			wlr_output = output->wlr_output;
		}

		// TODO: move state into wlr_output
		if (output->state == DRM_OUTPUT_DISCONNECTED &&
			conn->connection == DRM_MODE_CONNECTED) {

			wlr_log(L_INFO, "'%s' connected", output->wlr_output->name);
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
			wlr_log(L_INFO, "Sending modesetting signal for '%s'", output->wlr_output->name);
			wl_signal_emit(&state->backend->events.output_add, wlr_output);
		} else if (output->state == DRM_OUTPUT_CONNECTED &&
			conn->connection != DRM_MODE_CONNECTED) {

			wlr_log(L_INFO, "'%s' disconnected", output->wlr_output->name);
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

	if (output->bo[1]) {
		gbm_surface_release_buffer(output->gbm, output->bo[1]);
		output->bo[1] = NULL;
	}

	output->pageflip_pending = false;
	if (output->state == DRM_OUTPUT_CONNECTED && state->session->active) {
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
		wlr_log(L_INFO, "Emmiting destruction signal for '%s'", output->wlr_output->name);
		wl_signal_emit(&state->backend->events.output_remove, output->wlr_output);
		break;
	case DRM_OUTPUT_DISCONNECTED:
		break;
	}
}
