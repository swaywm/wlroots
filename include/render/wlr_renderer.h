#ifndef RENDER_WLR_RENDERER_H
#define RENDER_WLR_RENDERER_H

#include <wlr/render/wlr_renderer.h>

bool wlr_renderer_bind_buffer(struct wlr_renderer *r, struct wlr_buffer *buffer);
/**
 * Get the DMA-BUF formats supporting rendering usage. Buffers allocated with
 * a format from this list may be attached via wlr_renderer_bind_buffer.
 */
const struct wlr_drm_format_set *wlr_renderer_get_dmabuf_render_formats(
	struct wlr_renderer *renderer);

#endif
