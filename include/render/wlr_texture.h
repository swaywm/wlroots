#ifndef RENDER_WLR_TEXTURE_H
#define RENDER_WLR_TEXTURE_H

#include <wlr/render/wlr_texture.h>

/**
 * Refresh the texture contents from the underlying buffer storage.
 *
 * If the texture refers to external memory (i.e. memory owned by another
 * process), the renderer may need a wlr_texture_invalidate call to make
 * external changes visible to the texture.
 *
 * This operation must not perform any copy.
 *
 * If the texture cannot be invalidated, false is returned. If the texture
 * doesn't need to be invalidated for external changes to be visible, true is
 * immediately returned.
 */
bool wlr_texture_invalidate(struct wlr_texture *texture);

#endif
