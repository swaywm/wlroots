#ifndef WLR_TYPES_WLR_XCURSOR_MANAGER_H
#define WLR_TYPES_WLR_XCURSOR_MANAGER_H

#include <wayland-server.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/xcursor.h>
#include <wlr/util/edges.h>

/**
 * A scaled XCursor theme.
 */
struct wlr_xcursor_manager_theme {
	uint32_t scale;
	struct wlr_xcursor_theme *theme;
	struct wl_list link;
};

/**
 * Manage multiple XCursor themes with different scales and set `wlr_cursor`
 * images.
 *
 * This manager can be used to display cursor images on multiple outputs having
 * different scale factors.
 */
struct wlr_xcursor_manager {
	char *name;
	uint32_t size;
	struct wl_list scaled_themes; // wlr_xcursor_manager_theme::link
};

/**
 * Create a new XCursor manager. After initialization, scaled themes need to be
 * loaded with `wlr_xcursor_manager_load`. `size` is the unscaled cursor theme
 * size.
 */
struct wlr_xcursor_manager *wlr_xcursor_manager_create(const char *name,
	uint32_t size);

void wlr_xcursor_manager_destroy(struct wlr_xcursor_manager *manager);

int wlr_xcursor_manager_load(struct wlr_xcursor_manager *manager,
	uint32_t scale);

struct wlr_xcursor *wlr_xcursor_manager_get_xcursor(
	struct wlr_xcursor_manager *manager, const char *name, uint32_t scale);

/**
 * Set a `wlr_cursor` image. The manager uses all currently loaded scaled
 * themes.
 */
void wlr_xcursor_manager_set_cursor_image(struct wlr_xcursor_manager *manager,
	const char *name, struct wlr_cursor *cursor);

/**
 * Get the name of the cursor image for the given edges.
 */
const char *wlr_xcursor_manager_get_resize_name(enum wlr_edges edges);


#endif
