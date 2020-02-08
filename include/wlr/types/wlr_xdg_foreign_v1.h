/*
 * This an unstable interface of wlroots. No guarantees are made regarding the
 * future consistency of this API.
 */
#ifndef WLR_USE_UNSTABLE
#error "Add -DWLR_USE_UNSTABLE to enable unstable wlroots features"
#endif

#ifndef WLR_TYPES_WLR_XDG_FOREIGN_V1_H
#define WLR_TYPES_WLR_XDG_FOREIGN_V1_H

#include <wayland-server-core.h>

#define WLR_XDG_FOREIGN_V1_HANDLE_SIZE 37

struct wlr_xdg_foreign_v1 {
	struct {
		struct wl_global *global;
		struct wl_list resources;
		struct wl_list clients;
	} exporter, importer;

	struct wl_listener display_destroy;

	struct {
		struct wl_signal destroy;
	} events;

	void *data;
};

struct wlr_xdg_exporter_v1 {
	struct wlr_xdg_foreign_v1 *foreign;
	struct wl_resource *resource;

	struct wl_list link; // wlr_xdg_foreign_v1::exporter::clients

	struct wl_list exports;
};

struct wlr_xdg_importer_v1 {
	struct wlr_xdg_foreign_v1 *foreign;
	struct wl_resource *resource;

	struct wl_list link; // wlr_xdg_foreign_v1::importer::clients

	struct wl_list imports;
};

struct wlr_xdg_exported_v1 {
	struct wlr_surface *surface;
	struct wl_resource *resource;

	struct wl_list link; // wlr_xdg_exporter_v1::exports

	struct wl_listener xdg_surface_unmap;

	struct wl_list imports;
	char handle[WLR_XDG_FOREIGN_V1_HANDLE_SIZE];
};

struct wlr_xdg_imported_v1 {
	struct wlr_xdg_exported_v1 *exported;
	struct wl_resource *resource;

	struct wl_list export_link; // wlr_xdg_exported_v1::imports
	struct wl_list link; // wlr_xdg_importer_v1::imports

	struct wl_list children;
};

struct wlr_xdg_imported_child_v1 {
	struct wlr_xdg_imported_v1 *imported;
	struct wlr_surface *surface;

	struct wl_list link; // wlr_xdg_imported_v1::children

	struct wl_listener xdg_surface_unmap;
	struct wl_listener xdg_toplevel_set_parent;
};

struct wlr_xdg_foreign_v1 *wlr_xdg_foreign_v1_create(
		struct wl_display *display);
void wlr_xdg_foreign_v1_destroy(struct wlr_xdg_foreign_v1 *foreign);

#endif
