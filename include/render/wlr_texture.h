#ifndef RENDER_WLR_TEXTURE_H
#define RENDER_WLR_TEXTURE_H

#include <wlr/render/wlr_texture.h>

/**
 * Create a new texture from a buffer.
 *
 * Should not be called in a rendering block like renderer_begin()/end() or
 * between attaching a renderer to an output and committing it.
 */
struct wlr_texture *wlr_texture_from_buffer(struct wlr_renderer *renderer,
	struct wlr_buffer *buffer);

#endif
