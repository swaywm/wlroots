/*
 * This an unstable interface of wlroots. No guarantees are made regarding the
 * future consistency of this API.
 */
#ifndef WLR_USE_UNSTABLE
#error "Add -DWLR_USE_UNSTABLE to enable unstable wlroots features"
#endif

#ifndef WLR_TYPES_WLR_XCURSOR_MANAGER_H
#define WLR_TYPES_WLR_XCURSOR_MANAGER_H

#include <wayland-server-core.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/xcursor.h>

struct wlr_renderer;

/**
 * An XCursor theme at a particular scale factor of the base size.
 */
struct wlr_xcursor_manager_theme {
	float scale;
	struct wlr_xcursor_theme *theme;
	struct wl_list link;
};

/**
 * wlr_xcursor_manager dynamically loads xcursor themes at sizes necessary for
 * use on outputs at arbitrary scale factors. You should call
 * wlr_xcursor_manager_load for each output you will show your cursor on, with
 * the scale factor parameter set to that output's scale factor.
 */
struct wlr_xcursor_manager {
	char *name;
	uint32_t size;
	struct wl_list scaled_themes; // wlr_xcursor_manager_theme::link

	struct wlr_renderer *renderer;
	struct wl_listener renderer_destroy;
};

/**
 * Creates a new XCursor manager with the given xcursor theme name and base size
 * (for use when scale=1).
 */
struct wlr_xcursor_manager *wlr_xcursor_manager_create(const char *name,
	uint32_t size, struct wlr_renderer *renderer);

void wlr_xcursor_manager_destroy(struct wlr_xcursor_manager *manager);

/**
 * Ensures an xcursor theme at the given scale factor is loaded in the manager.
 */
int wlr_xcursor_manager_load(struct wlr_xcursor_manager *manager,
	float scale);

/**
 * Retrieves a wlr_xcursor reference for the given cursor name at the given
 * scale factor, or NULL if this wlr_xcursor_manager has not loaded a cursor
 * theme at the requested scale.
 */
struct wlr_xcursor *wlr_xcursor_manager_get_xcursor(
	struct wlr_xcursor_manager *manager, const char *name, float scale);

/**
 * Retrieves the cached texture of this xcursor image from the
 * wlr_xcursor_manager.
 *
 * The wlr_xcursor_manager must have been created with a wlr_renderer and the
 * wlr_xcursor_image must have come from a wlr_xcursor returned from
 * wlr_xcursor_manager_get_xcursor.
 *
 * The returned texture is owned by the xcursor manager. You should not save or
 * destroy it.
 * The manager makes use of the wlr_xcursor_image.userdata field. You must not
 * modify this field yourself.
 */
struct wlr_texture *wlr_xcursor_manager_get_texture(
	struct wlr_xcursor_manager *manager, struct wlr_xcursor_image *image);

#endif
