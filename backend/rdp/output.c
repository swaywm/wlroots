#include <assert.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <stdlib.h>
#include <wayland-server.h>
#include <wlr/types/wlr_output.h>
#include <wlr/interfaces/wlr_output.h>
#include <wlr/util/log.h>
#include <wlr/render/wlr_renderer.h>
#include "backend/rdp.h"
#include "util/signal.h"

static struct wlr_rdp_output *rdp_output_from_output(
		struct wlr_output *wlr_output) {
	assert(wlr_output_is_rdp(wlr_output));
	return (struct wlr_rdp_output *)wlr_output;
}

static EGLSurface egl_create_surface(struct wlr_egl *egl, unsigned int width,
		unsigned int height) {
	EGLint attribs[] = {
		EGL_WIDTH, width,
		EGL_HEIGHT, height,
		EGL_NONE,
	};
	EGLSurface surf = eglCreatePbufferSurface(egl->display, egl->config, attribs);
	if (surf == EGL_NO_SURFACE) {
		wlr_log(WLR_ERROR, "Failed to create EGL surface");
		return EGL_NO_SURFACE;
	}
	return surf;
}

static bool output_set_custom_mode(struct wlr_output *wlr_output, int32_t width,
		int32_t height, int32_t refresh) {
	struct wlr_rdp_output *output =
		rdp_output_from_output(wlr_output);
	struct wlr_rdp_backend *backend = output->backend;

	if (refresh <= 0) {
		refresh = 60 * 1000; // 60 Hz
	}

	wlr_egl_destroy_surface(&backend->egl, output->egl_surface);

	output->egl_surface = egl_create_surface(&backend->egl, width, height);
	if (output->egl_surface == EGL_NO_SURFACE) {
		wlr_log(WLR_ERROR, "Failed to recreate EGL surface");
		wlr_output_destroy(wlr_output);
		return false;
	}

	output->frame_delay = 1000000 / refresh;

	if (output->shadow_surface) {
		pixman_image_unref(output->shadow_surface);
	}
	output->shadow_surface = pixman_image_create_bits(PIXMAN_x8r8g8b8,
			width, height, NULL, width * 4);

	wlr_output_update_custom_mode(&output->wlr_output, width, height, refresh);
	return true;
}

static void output_transform(struct wlr_output *wlr_output,
		enum wl_output_transform transform) {
	struct wlr_rdp_output *output =
		rdp_output_from_output(wlr_output);
	output->wlr_output.transform = transform;
}

static bool output_attach_render(struct wlr_output *wlr_output,
		int *buffer_age) {
	struct wlr_rdp_output *output =
		rdp_output_from_output(wlr_output);
	return wlr_egl_make_current(&output->backend->egl, output->egl_surface,
		buffer_age);
}

static bool rfx_swap_buffers(
		struct wlr_rdp_output *output, pixman_region32_t *damage) {
	if (!pixman_region32_not_empty(damage)) {
		return true;
	}
	struct wlr_rdp_peer_context *context = output->context;
	freerdp_peer *peer = context->peer;
	rdpUpdate *update = peer->update;

	Stream_Clear(context->encode_stream);
	Stream_SetPosition(context->encode_stream, 0);
	int width = damage->extents.x2 - damage->extents.x1;
	int height = damage->extents.y2 - damage->extents.y1;

	SURFACE_BITS_COMMAND cmd;
	cmd.skipCompression = TRUE;
	cmd.destLeft = damage->extents.x1;
	cmd.destTop = damage->extents.y1;
	cmd.destRight = damage->extents.x2;
	cmd.destBottom = damage->extents.y2;
	cmd.bmp.bpp = pixman_image_get_depth(output->shadow_surface);
	cmd.bmp.codecID = peer->settings->RemoteFxCodecId;
	cmd.bmp.width = width;
	cmd.bmp.height = height;

	uint32_t *ptr = pixman_image_get_data(output->shadow_surface) +
		damage->extents.x1 + damage->extents.y1 *
		(pixman_image_get_stride(output->shadow_surface) / sizeof(uint32_t));

	RFX_RECT *rfx_rect;
	int nrects;
	pixman_box32_t *rects =
		pixman_region32_rectangles(damage, &nrects);
	rfx_rect = realloc(context->rfx_rects, nrects * sizeof(*rfx_rect));
	if (rfx_rect == NULL) {
		wlr_log(WLR_ERROR, "RDP swap buffers failed: could not realloc rects");
		return false;
	}
	context->rfx_rects = rfx_rect;

	for (int i = 0; i < nrects; ++i) {
		pixman_box32_t *region = &rects[i];
		rfx_rect = &context->rfx_rects[i];
		rfx_rect->x = region->x1 - damage->extents.x1;
		rfx_rect->y = region->y1 - damage->extents.y1;
		rfx_rect->width = region->x2 - region->x1;
		rfx_rect->height = region->y2 - region->y1;
	}

	rfx_compose_message(context->rfx_context, context->encode_stream,
			context->rfx_rects, nrects, (BYTE *)ptr, width, height,
			pixman_image_get_stride(output->shadow_surface));
	cmd.bmp.bitmapDataLength = Stream_GetPosition(context->encode_stream);
	cmd.bmp.bitmapData = Stream_Buffer(context->encode_stream);

	update->SurfaceBits(update->context, &cmd);
	return true;
}

static bool nsc_swap_buffers(
		struct wlr_rdp_output *output, pixman_region32_t *damage) {
	struct wlr_rdp_peer_context *context = output->context;
	freerdp_peer *peer = context->peer;
	rdpUpdate *update = peer->update;

	Stream_Clear(context->encode_stream);
	Stream_SetPosition(context->encode_stream, 0);
	int width = damage->extents.x2 - damage->extents.x1;
	int height = damage->extents.y2 - damage->extents.y1;

	SURFACE_BITS_COMMAND cmd;
	cmd.skipCompression = TRUE;
	cmd.destLeft = damage->extents.x1;
	cmd.destTop = damage->extents.y1;
	cmd.destRight = damage->extents.x2;
	cmd.destBottom = damage->extents.y2;
	cmd.bmp.bpp = pixman_image_get_depth(output->shadow_surface);
	cmd.bmp.codecID = peer->settings->NSCodecId;
	cmd.bmp.width = width;
	cmd.bmp.height = height;

	uint32_t *ptr = pixman_image_get_data(output->shadow_surface) +
		damage->extents.x1 + damage->extents.y1 *
		(pixman_image_get_stride(output->shadow_surface) / sizeof(uint32_t));

	nsc_compose_message(context->nsc_context, context->encode_stream,
			(BYTE *)ptr, width, height,
			pixman_image_get_stride(output->shadow_surface));

	cmd.bmp.bitmapDataLength = Stream_GetPosition(context->encode_stream);
	cmd.bmp.bitmapData = Stream_Buffer(context->encode_stream);

	update->SurfaceBits(update->context, &cmd);
	return true;
}

static bool output_commit(struct wlr_output *wlr_output) {
	struct wlr_rdp_output *output =
		rdp_output_from_output(wlr_output);
	bool ret = false;

	pixman_region32_t output_region;
	pixman_region32_init(&output_region);
	pixman_region32_union_rect(&output_region, &output_region,
		0, 0, wlr_output->width, wlr_output->height);

	pixman_region32_t *damage = &output_region;
	if (wlr_output->pending.committed & WLR_OUTPUT_STATE_DAMAGE) {
		damage = &wlr_output->pending.damage;
	}

	int x = damage->extents.x1;
	int y = damage->extents.y1;
	int width = damage->extents.x2 - damage->extents.x1;
	int height = damage->extents.y2 - damage->extents.y1;

	// Update shadow buffer
	struct wlr_renderer *renderer =
		wlr_backend_get_renderer(&output->backend->backend);
	// TODO performance: add support for flags
	ret = wlr_renderer_read_pixels(renderer, WL_SHM_FORMAT_XRGB8888,
		NULL, pixman_image_get_stride(output->shadow_surface),
		width, height, x, y, x, y,
		pixman_image_get_data(output->shadow_surface));
	if (!ret) {
		goto out;
	}

	// Send along to clients
	rdpSettings *settings = output->context->peer->settings;
	if (settings->RemoteFxCodec) {
		ret = rfx_swap_buffers(output, damage);
	} else if (settings->NSCodec) {
		ret = nsc_swap_buffers(output, damage);
	} else {
		// This would perform like ass so why bother
		wlr_log(WLR_ERROR, "Raw updates are not supported; use rfx or nsc");
		ret = false;
	}
	if (!ret) {
		goto out;
	}

	wlr_output_send_present(wlr_output, NULL);

out:
	pixman_region32_fini(&output_region);
	return ret;
}

static void output_destroy(struct wlr_output *wlr_output) {
	struct wlr_rdp_output *output =
		rdp_output_from_output(wlr_output);
	if (output->frame_timer) {
		wl_event_source_remove(output->frame_timer);
	}
	wlr_egl_destroy_surface(&output->backend->egl, output->egl_surface);
	if (output->shadow_surface) {
		pixman_image_unref(output->shadow_surface);
	}
	free(output);
}

static const struct wlr_output_impl output_impl = {
	.set_custom_mode = output_set_custom_mode,
	.transform = output_transform,
	.destroy = output_destroy,
	.attach_render = output_attach_render,
	.commit = output_commit,
};

bool wlr_output_is_rdp(struct wlr_output *wlr_output) {
	return wlr_output->impl == &output_impl;
}

static int signal_frame(void *data) {
	struct wlr_rdp_output *output = data;
	wlr_output_send_frame(&output->wlr_output);
	wl_event_source_timer_update(output->frame_timer, output->frame_delay);
	return 0;
}

struct wlr_rdp_output *wlr_rdp_output_create(struct wlr_rdp_backend *backend,
		struct wlr_rdp_peer_context *context, unsigned int width,
		unsigned int height) {
	struct wlr_rdp_output *output =
		calloc(1, sizeof(struct wlr_rdp_output));
	if (output == NULL) {
		wlr_log(WLR_ERROR, "Failed to allocate wlr_rdp_output");
		return NULL;
	}
	output->backend = backend;
	output->context = context;
	wlr_output_init(&output->wlr_output, &backend->backend, &output_impl,
		backend->display);
	struct wlr_output *wlr_output = &output->wlr_output;

	output->egl_surface = egl_create_surface(&backend->egl, width, height);
	if (output->egl_surface == EGL_NO_SURFACE) {
		wlr_log(WLR_ERROR, "Failed to create EGL surface");
		goto error;
	}

	output_set_custom_mode(wlr_output, width, height, 0);
	strncpy(wlr_output->make, "RDP", sizeof(wlr_output->make));
	strncpy(wlr_output->model, "RDP", sizeof(wlr_output->model));
	snprintf(wlr_output->name, sizeof(wlr_output->name), "RDP-%d",
		wl_list_length(&backend->clients));

	if (!wlr_egl_make_current(&output->backend->egl, output->egl_surface,
			NULL)) {
		goto error;
	}

	wlr_renderer_begin(backend->renderer, wlr_output->width, wlr_output->height);
	wlr_renderer_clear(backend->renderer, (float[]){ 1.0, 1.0, 1.0, 1.0 });
	wlr_renderer_end(backend->renderer);

	struct wl_event_loop *ev = wl_display_get_event_loop(backend->display);
	output->frame_timer = wl_event_loop_add_timer(ev, signal_frame, output);
	wl_event_source_timer_update(output->frame_timer, output->frame_delay);
	wlr_output_update_enabled(wlr_output, true);
	wlr_signal_emit_safe(&backend->backend.events.new_output, wlr_output);
	return output;

error:
	wlr_output_destroy(&output->wlr_output);
	return NULL;
}
