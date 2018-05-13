#ifndef TYPES_WLR_XDG_SHELL_V6_H
#define TYPES_WLR_XDG_SHELL_V6_H

#include <wayland-server.h>
#include <wlr/types/wlr_xdg_shell_v6.h>
#include "xdg-shell-unstable-v6-protocol.h"

struct wlr_xdg_positioner_v6_resource {
	struct wl_resource *resource;
	struct wlr_xdg_positioner_v6 attrs;
};

#define XDG_TOPLEVEL_ROLE "xdg_toplevel_v6"
#define XDG_POPUP_ROLE "xdg_popup_v6"

uint32_t xdg_surface_v6_schedule_configure(struct wlr_xdg_surface_v6 *surface);
struct wlr_xdg_surface_v6 *xdg_surface_v6_create(
	struct wlr_xdg_client_v6 *client, struct wlr_surface *surface,
	uint32_t id);
void xdg_surface_unmap(struct wlr_xdg_surface_v6 *surface);
void xdg_surface_destroy(struct wlr_xdg_surface_v6 *surface);

void xdg_positioner_v6_create(struct wlr_xdg_client_v6 *client, uint32_t id);
struct wlr_xdg_positioner_v6_resource *xdg_positioner_from_resource(
	struct wl_resource *resource);

void xdg_popup_v6_create(struct wlr_xdg_surface_v6 *xdg_surface,
	struct wlr_xdg_surface_v6 *parent,
	struct wlr_xdg_positioner_v6_resource *positioner, int32_t id);
void xdg_surface_v6_popup_committed(struct wlr_xdg_surface_v6 *surface);
struct wlr_xdg_popup_grab_v6 *xdg_shell_popup_grab_from_seat(
	struct wlr_xdg_shell_v6 *shell, struct wlr_seat *seat);
void xdg_popup_destroy(struct wlr_xdg_surface_v6 *surface);

void xdg_toplevel_v6_create(struct wlr_xdg_surface_v6 *xdg_surface,
	uint32_t id);
void xdg_surface_v6_toplevel_committed(struct wlr_xdg_surface_v6 *surface);
void xdg_toplevel_v6_send_configure(struct wlr_xdg_surface_v6 *surface,
	struct wlr_xdg_surface_v6_configure *configure);
void xdg_toplevel_v6_ack_configure(struct wlr_xdg_surface_v6 *surface,
	struct wlr_xdg_surface_v6_configure *configure);
bool xdg_surface_v6_toplevel_state_compare(struct wlr_xdg_toplevel_v6 *state);
void xdg_toplevel_destroy(struct wlr_xdg_surface_v6 *surface);

#endif
